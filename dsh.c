#include "dsh.h"

void seize_tty(pid_t callingprocess_pgid); /* Grab control of the terminal for the calling process pgid.  */
void continue_job(job_t *j); /* resume a stopped job */
void spawn_job(job_t *j, bool fg); /* spawn a new job */
job_t* allJobs; // global list of all called jobs
job_t* endJobs; // pointer to the end of the jobs list
int allJobsSize; // for keeping track of its length

#define RD 0
#define WR 1

/* Sets the process group id for a given job and process */
int set_child_pgid(job_t *j, process_t *p)
{
	printf("%d",j->pgid);
if (j->pgid < 0) /* first child: use its pid for job pgid */
        j->pgid = p->pid;
    return(setpgid(p->pid,j->pgid));
}

/* Creates the context for a new child by setting the pid, pgid and tcsetpgrp */
void new_child(job_t *j, process_t *p, bool fg)
{
         /* establish a new process group, and put the child in
          * foreground if requested
          */

         /* Put the process into the process group and give the process
          * group the terminal, if appropriate.  This has to be done both by
          * the dsh and in the individual child processes because of
          * potential race conditions.
          * */

         p->pid = getpid();

         /* also establish child process group in child to avoid race (if parent has not done it yet). */
         if(!setpgid(0,j->pgid)){
            if(fg) // if fg is set
                seize_tty(j->pgid); // assign the terminal
        }

         /* Set the handling for job control signals back to the default. */
         signal(SIGTTOU, SIG_DFL);
}

/* Spawning a process with job control. fg is true if the
 * newly-created process is to be placed in the foreground.
 * (This implicitly puts the calling process in the background,
 * so watch out for tty I/O after doing this.) pgid is -1 to
 * create a new job, in which case the returned pid is also the
 * pgid of the new job.  Else pgid specifies an existing job's
 * pgid: this feature is used to start the second or
 * subsequent processes in a pipeline.
 * */

void spawn_job(job_t *j, bool fg)
{

	pid_t pid;
	process_t *p;
	int prev_fds;
	int final_out_fds;
    // redirection variables
    int bak;
    int new;

    //int new_in;
    //int back_in;
    bool redirect_out = false;
    //bool redirect_in = false;
	for(p = j->first_process; p; p = p->next) {

        // check for stdio redirection
        if (p->ofile != NULL) {
            redirect_out = true;
        }
        //if (p->ifile != NULL) {
        //    redirect_in = true;
        //}
		/* Builtin commands are already taken care earlier */
		int fds[2];
		if(p != j->first_process) {
			pipe(fds);
		}
            //dup2(store[1], fds[1]);
        //else
            //dup2(0, fds[0]);

		switch (pid = fork()) {
			 int status;
          case -1: /* fork failure */
            perror("fork");
            exit(EXIT_FAILURE);
				break;

          case 0: /* child process */
				printf("Child: %d; command: %s\n", getpid(), p->argv[0]);
            p->pid = getpid();
            new_child(j, p, fg);
                // redirecting stdout
                if (redirect_out == true) {
                    bak = dup(1);
                    new = open(p->ofile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IRGRP | S_IWOTH | S_IROTH);
                    dup2(new, 1);
                    close(new);
                }
                // redirecting stdin
                /*if (redirect_in == true) {
                    back_in = dup(0);
                    new_in = open(p->ifile, O_RDONLY);
                    dup2(new_in, 0);
                    close(new_in);
                }*/

				if(p == j->first_process) {
					dup2(WR,final_out_fds);
				}
				if(p->next != NULL) {
					dup2(fds[RD],prev_fds);
				}
				close(fds[RD]);
				if(j->first_process != p && p->next != NULL){/*interior process: sends output to buffer*/
					dup2(RD, prev_fds);
					dup2(fds[WR],WR);
				} else if(p->next == NULL){/*final process: maps output back to parent output*/
					dup2(final_out_fds,WR);
				}
				execve(p->argv[0], p->argv,0);
         	perror("New child should have done an exec");
         	exit(EXIT_FAILURE);  /* NOT REACHED */
            break;    /* NOT REACHED */

          default: /* parent */
 				/* establish child process group */
				printf("Parent: %d; PID set to %d\n", getpid(), pid);
				close(fds[RD]);
				waitpid(pid, &status, 0);
				close(fds[WR]);
		   	p->pid = pid;
            set_child_pgid(j, p);
            p->status = 0;
            p->completed = true;
				break;
         }
        // return redirection back to standard
        if(redirect_out == true) {
            dup2(bak, 1);
            close(bak);
        }

        /*if(redirect_in == true) {
            dup2(back_in, 0);
            close(back_in);
        }*/
		if(p->next == NULL){
	   	seize_tty(getpid()); // assign the terminal back to dsh
		}
	}
}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *j)
{
     if(kill(-j->pgid, SIGCONT) < 0)
          perror("kill(SIGCONT)");
}


/*
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.
 */
bool builtin_cmd(job_t *last_job, int argc, char **argv)
{

	    /* check whether the cmd is a built in command
        */
        /* Apparently these cannot be piped (from dsh example) so they should always be the first process of the job for completion updating purposes */
        if (!strcmp(argv[0], "quit")) {
            /* Your code here */
            exit(EXIT_SUCCESS);
            last_job->first_process->completed = true;
            last_job->first_process->status = 0;
            return true;
		  }
        else if (!strcmp("jobs", argv[0])) {
            /* Your code here */
            /* all previously completed jobs */
            if(allJobs == NULL){
                printf("No jobs in list\n");
            }else{
                job_t *cycle;
                cycle = allJobs;
                int i;
                int listSize;
                listSize = 0;
                for(i = 0; i < allJobsSize; i++){
                    char* s[20];
                    if(job_is_completed(cycle)){
                        sprintf(s, "%d. %s (PID: %d)\n STATUS: COMPLETE\n", (listSize+1), cycle->commandinfo, (int) cycle->first_process->pid);
                        if(allJobsSize == 1){
                            allJobs = NULL;
                        }else{
                            delete_job(allJobs, cycle);
                        }
                        allJobsSize  -= 1;
                        i -= 1;
                    }else if(job_is_stopped(cycle)){
                        sprintf(s, "%d. %s (PID: %d)\n STATUS: STOPPED\n", (listSize+1), cycle->commandinfo, (int) cycle->first_process->pid);
                    }
                    printf(s);
                    cycle = cycle->next;
                    listSize++;
                }
            }
            last_job->first_process->completed = true;
            last_job->first_process->status = 0;
            return true;
        }
		  else if (!strcmp("cd", argv[0])) {
			   exit(1);
            /* Your code here */
            char* path = argv[1];
            int ret = chdir(path);
            last_job->first_process->completed = true;
            last_job->first_process->status = 0;
            return true;
        }
        else if (!strcmp("bg", argv[0])) {
            /* Your code here */
            last_job->first_process->completed = true;
            last_job->first_process->status = 0;
            return true;
        }
        else if (!strcmp("fg", argv[0])) {
            /* Your code here */
            last_job->first_process->completed = true;
            last_job->first_process->status = 0;
            return true;
        }
        return false;       /* not a builtin command */
}

/* Build prompt messaage */
char* promptmsg()
{
    /* Modify this to include pid */
    char* s[20];
    sprintf(s, "dsh-%d$ ", (int) getpid());
    return s;
}

int main()
{

	init_dsh();
	DEBUG("Successfully initialized\n");

	while(1) {
        job_t *j = NULL;
		if(!(j = readcmdline(promptmsg()))) {
			if (feof(stdin)) { /* End of file (ctrl-d) */
				fflush(stdout);
				printf("\n");
				exit(EXIT_SUCCESS);
           		}
			continue; /* NOOP; user entered return or spaces with return */
		}

        /* Only for debugging purposes to show parser output; turn off in the
         * final code */
        //if(PRINT_INFO) print_job(j);

        /* Your code goes here */
        /* You need to loop through jobs list since a command line can contain ;*/
        /* Check for built-in commands */
        /* If not built-in */
            /* If job j runs in foreground */
            /* spawn_job(j,true) */
            /* else */
            /* spawn_job(j,false) */
        job_t *i;
        i = j;
        process_t *n;

        //give new jobs gpid's (the terminal here)
        while(i->next != NULL){
            n = i->first_process;
            while(n->next !=NULL){
                n->pid = getpid();
                n = n->next;
            }
            if(n->next == NULL){
                n->pid = getpid();
            }
            i->pgid = getpid();
            i = i->next;
        }
        if (i->next == NULL){
            n = i->first_process;
            while(n->next != NULL){
                n->pid = getpid();
                n = n->next;
            }
            if(n->next ==NULL){
                n->pid = getpid();
            }
            i->pgid = getpid();
        }

        //read command line here
        job_t* jobCheck;
        process_t* procCheck;
        jobCheck = j;
        while(jobCheck->next!=NULL){
            procCheck = jobCheck->first_process;
            while(procCheck->next != NULL){
                if(!(builtin_cmd(jobCheck,procCheck->argc, procCheck->argv))){
                        spawn_job(jobCheck, !(jobCheck->bg));
                }
                procCheck = procCheck->next;
            }
            if(procCheck->next == NULL){
                if(!(builtin_cmd(jobCheck,procCheck->argc, procCheck->argv))){
                    spawn_job(jobCheck, !(jobCheck->bg));
                }
                if(allJobs == NULL){
                    allJobs = jobCheck;
                    endJobs = allJobs;
                }else{
                    endJobs->next = jobCheck;
                    endJobs = endJobs->next; //at the end of a job after last process, add that job to allJobs
                }
                allJobsSize++;
            }
            jobCheck = jobCheck->next;
        }
        if(jobCheck->next==NULL){
            procCheck = jobCheck->first_process;
            while(procCheck->next != NULL){
                if(!(builtin_cmd(jobCheck,procCheck->argc, procCheck->argv))){
                    spawn_job(jobCheck, !(jobCheck->bg));
                }
                procCheck = procCheck->next;
            }
            if(procCheck->next == NULL){
                if(!(builtin_cmd(jobCheck,procCheck->argc, procCheck->argv))){
                    spawn_job(jobCheck, !(jobCheck->bg));
                }
                if(allJobs == NULL){
                    allJobs = jobCheck;
                    endJobs = allJobs;
                }else{
                    endJobs->next = jobCheck;
                    endJobs = endJobs->next; //at the end of a job after last process, add that job to allJobs
                }
                allJobsSize++;
            }
        }
    }
}
