/**************************************************
 * Title: SP-Project 1  -  My Shell        
 * Summary: 'My Shell' Program for studying the co-
 -cepts of system-level process control, process s-
 -ignalling, interprocess communication and runnin-
 -g processes and jobs in the background in Linux
 Shell.
 *  |Date              |Author             |Version
	|2022-04-11        |Park Junhyeok      |1.0.0
**************************************************/

/****************** Declaration ******************/
/* Headers */
#include "myshell.h"
#include <errno.h>

/* Global Variables */
volatile sig_atomic_t PID;					/* global variable for reaping routine */
int pipe_flag;								/* is cmdline has at least one pipe? */
int read_flag;								/* flag for sigint_handler */

/* Subroutines for the Read-Eval Cycle */
void eval(char *cmdline, int passed_fd, int cnt);
int buffering(char *cmdline, char *buf, int *idx);
int parseline(char *buf, char **argv);
void pipe_check(char *cmdline);
int bg_check(char *cmdline);
int foreground_check(void);

/* Subroutines for the Built-In-Commands */
int builtin_command(char **argv);
int fgbgkill_command(char **argv, int option);
int cd_command(char **argv);
int jobs_command(void);

/* Subroutines for the Signal Handling */
void sigint_handler(int sig);
void sigtstp_handler(int sig);
void sigchld_handler(int sig);


/**************** Implementation *****************/
/***    Subroutines for the Read-Eval Cycle    ***/
/* Main procedure of 'My Shell' */
int main(void) {
	char cmdline[MAXLINE];					// command line

	init_queue();
	Signal(SIGTSTP, sigtstp_handler);		// Ctrl+Z Handling
	Signal(SIGCHLD, sigchld_handler);		// SIGCHLD Handling
	Signal(SIGINT, sigint_handler);			// Ctrl+C Handling

	while (1) {								// Read-Eval Cycle
		read_flag = FALSE;

		Sio_puts("> ");
		Fgets(cmdline, MAXLINE, stdin);
		if (feof(stdin)) exit(0);
		read_flag = TRUE;					// flag for Ctrl+C Handling
		
		for (int i = 0; cmdline[i]; i++) {
			if (cmdline[i] == '&') {		// this routine is for identifying any input forms
				cmdline[i] = '\0';			// of ampersand. for example,   1) ls &    2) ls&
				strcat(cmdline, " &\n");	// if ampersand is attached, then space it!
				break;
			}
		}

		pipe_check(cmdline);				// is cmdline has at least one pipe?
		eval(cmdline, 0, 0);				// evaluate the command
	}

	return 0;
}

/* Evaluate the command line with possible recursion */
void eval( 
	char *cmdline,							// command line read in the main procedure
	int passed_fd,							// file descriptor passed from parent to child (for pipe)
	int cnt									// how many times eval func has been called (for pipe)
) {
	char *argv[MAXARGS];						// arguments list for Execvp() func
	char buf[MAXLINE];							// buffer holds the modified command line
	pid_t pid;									// process id of new generated process
	int bg, idx, fd[2], last_flag, temp_fd;		// these are important variables
	Job *temp_job;								// for the implementation of eval function
	sigset_t mask_all, mask_one, mask_two, prev_one;

	if (pipe_flag) Pipe(fd);					// if pipe situation, let's construct the pipeline
	last_flag = buffering(cmdline, buf, &idx);	// buff the cmdline until meeting | or \0 (for pipe)
	bg = parseline(buf, argv);					// parse buffer and insert into argv List
	bg = bg_check(cmdline);						// check if it's background situation one more time 
	if (argv[0] == NULL) return;				// ignore empty lines

	Sigset_setting(&mask_all, &mask_one, &mask_two);	// setting for an explicit signal masking

	if (!builtin_command(argv)) {				// check if argv is the built-in-command such as cd
		if (bg) Sigprocmask(SIG_BLOCK, &mask_one, &prev_one);	// prevent the race problem when bg

		/* Child Process */
		if ((pid = Fork()) == 0) {							
			Sigprocmask(SIG_SETMASK, &prev_one, NULL);
			Sigprocmask(SIG_BLOCK, &mask_two, &prev_one);	// block SIGINT/TSTP for child process
															// these will be handled via handlers!
			if (!last_flag) {
				Close(fd[0]);
				Dup2(passed_fd, STDIN_FILENO);			// file descriptor passing routine for when
				Dup2(fd[1], STDOUT_FILENO);				// there are multiple pipes in the command
			}
			else Dup2(passed_fd, STDIN_FILENO);			// if 'the last cmd of pipe' or 'non-pipe cmd'

			Execvp(argv[0], argv);
		}
		/* Parent Process */
		if (!bg) {	/* Foreground Process */
			Sigprocmask(SIG_BLOCK, &mask_all, &prev_one);
			Enqueue(pid, Foreground, cmdline);		// push process into the Job queue, preparing for
													// the situation that fg process has been stopped
			PID = 0;
			while (!PID) {							// reaping chlid process with sigsuspend routine
				if (foreground_check())				// only reaping the foreground process, not stopped
					Sigsuspend(&prev_one);
				else break;
			}
		}
		else {		/* Background Process */
			Sigprocmask(SIG_BLOCK, &mask_all, NULL);
			Enqueue(pid, Background, cmdline);		// push process into the Job queue
			temp_job = Search_queue(pid);

			if (cnt == 0) Sio_printBGjob(temp_job->idx, pid);	// print the background process
		}
		/* Common routine for Foreground and Background */
		if (!last_flag) {							// file descriptor closing routine for when
			Close(fd[1]);							// there are multiple pipes in the command
			Close(passed_fd);
		}
		else {										// if 'the last cmd of pipe' or 'non-pipe cmd'
			if (cnt == 0) temp_fd = 1;					// if 'non-pipe cmd', then temp_fd is stdout
			else temp_fd = 0;							// if 'the last cmd of pipe', then stdin

			Dup2(STDIN_FILENO, passed_fd);			// restore the input file descriptor
			Dup2(STDOUT_FILENO, temp_fd);			// restore the output file descriptor
		}
		Sigprocmask(SIG_SETMASK, &prev_one, NULL);

		if (!last_flag)
			eval(cmdline + idx + 1, fd[0], cnt + 1);	// recursively call eval for pipelining
	}
	return;
}

/* Buffering - cut and buff 'cmdline' */
int buffering(char *cmdline, char *buf, int *idx) {		
	int i = *idx, last_flag = TRUE, j;

	for (i = 0; cmdline[i]; i++) {
		if (cmdline[i] == '\'') {		// if meet ' or ", replace all spaces with temp value -1
			cmdline[i] = ' ';										// until re-meeting ' or " 
			for (j = i + 1; cmdline[j] && cmdline[j] != '\''; j++) {
				if (cmdline[j] == ' ')
					cmdline[j] = -1;
			}
			cmdline[j] = ' ';
		}								// this routine is for handling some inputs like 'ab  c'
		if (cmdline[i] == '\"') {
			cmdline[i] = ' ';
			for (j = i + 1; cmdline[j] && cmdline[j] != '\"'; j++) {
				if (cmdline[j] == ' ')
					cmdline[j] = -1;	// value -1 will be replaced with 'space' in parseline func
			}
			cmdline[j] = ' ';
		}
		if (cmdline[i] == '|') {				// if meet |(pipe), cut the cmdline! (for recursion)
			last_flag = FALSE;					// meeting pipe means 'it's not the last cmd
			break;
		}
		buf[i] = cmdline[i];					// buffering
	}
	buf[i] = '\0'; *idx = i;				// Record idx variable for Recursive call in eval func

	return last_flag;
}

/* Parse the command line and build the argv array */
int parseline(char *buf, char **argv) {		
	char *delim;							// points to first space delimiter
	int argc, bg;							// number of args and background flag

	if (buf[strlen(buf) - 1] == ' ' ||
		buf[strlen(buf) - 1] == '\n')
		buf[strlen(buf) - 1] = ' ';			// replace trailing '\n' or ' ' with space
	else buf[strlen(buf)] = ' ';

	while (*buf && (*buf == ' '))			// ignore leading spaces
		buf++;

	argc = 0;
	while ((delim = strchr(buf, ' '))) {	// build the argv list
		argv[argc] = buf;
		*delim = '\0';

		for (int i = 0; argv[argc][i]; i++) {	// replace temp value -1
			if (argv[argc][i] == -1)				// with space! (see 'buffering' func)
				argv[argc][i] = ' ';
		}
		argc++;

		buf = delim + 1;
		while (*buf && (*buf == ' '))		// ignore spaces
			buf++;
	}
	argv[argc] = NULL;

	if (argc == 0)							// ignore blank line
		return 1;

	if ((bg = (*argv[argc - 1] == '&')) != 0) // should the job run in the background?
		argv[--argc] = NULL;

	return bg;
}

/* Is cmdline has at least one pipe? */
void pipe_check(char *cmdline) {		
	pipe_flag = FALSE;

	for (int i = 0; cmdline[i]; i++) {
		if (cmdline[i] == '|') {		// pipe found!
			pipe_flag = TRUE;
			break;
		}
	}
}

/* Is cmdline has ampersand symbol? */
int bg_check(char *cmdline) {			
	for (int i = 0; cmdline[i]; i++) {
		if (cmdline[i] == '&')			// this routine is added just for implementing
			return TRUE;					// phase 3! (not essential for phase 1 and 2)
	}
	return FALSE;
}

/* Check if any remaining foreground processes*/
int foreground_check(void) {				
	for (int i = 1; i < MAXJOBS; i++) {		// in the job queue
		if (queue[i].state == Foreground)	// this func is for Wait routine of fg processes
			return TRUE;
	}
	return FALSE;
}
/***  Subroutines for the Read-Eval Cycle End  ***/



/***   Subroutines for the Built-In-Commands   ***/
/* If first arg is a built_in command, run it and return true */
int builtin_command(char **argv) {	 
	if (!strcmp(argv[0], "cd"))			/* command for moving location (cd) */
		return cd_command(argv);
	if (!strcmp(argv[0], "&"))			/* ignore singleton & */
		return TRUE;
	if (!strcmp(argv[0], "jobs"))		/* command for printing the current job queue */
		return jobs_command();
	if (!strcmp(argv[0], "bg"))			/* bg command */
		return fgbgkill_command(argv, 0);
	if (!strcmp(argv[0], "fg"))			/* fg command */
		return fgbgkill_command(argv, 1);
	if (!strcmp(argv[0], "kill"))		/* kill command */
		return fgbgkill_command(argv, 2);
	if (!strcmp(argv[0], "exit"))		/* shell termination command */
		exit(0);

	return FALSE;						// not a builtin command 
}

/* Procedure for handling 'fg', 'bg', or 'kill' */
int fgbgkill_command(char **argv, int option) { 
	int idx, pid;
	Job *temp_job;
	sigset_t mask_one, prev_one;

	if (argv[1] == NULL || argv[1][0] != '%') {		// incorrect usage handling: No param or jobspec
		if (option != 2) printf("No Such Job\n");
		else printf("Incorrect Kill Usage\n");
		return TRUE;
	}

	argv[1] = &(argv[1][0]) + 1;
	idx = atoi(argv[1]);
	if (idx < 1 || idx >= MAXJOBS) {	// incorrect usage handling: with jobspec, but incorrect idx
		printf("No Such Job\n");
		return TRUE;
	}
	pid = queue[idx].pid;
	if (queue[idx].state == Invalid) {	// incorrect usage handling: with jobspec, but invalid elem
		printf("No Such Job\n");
		return TRUE;
	}

	Sigemptyset(&mask_one);
	Sigaddset(&mask_one, SIGCHLD);			// prevent the possible interruption by SIGCHLD signal
	Sigprocmask(SIG_BLOCK, &mask_one, &prev_one);

	if (option == 0) { /* bg command */
		Sio_printjob(idx, queue[idx].cmdline);		// print which job is switched to background
		Kill(pid, SIGCONT);							// continue the process
		Update_queue(pid, Background);
	}
	else if (option == 1) { /* fg command */
		Sio_printjob(idx, queue[idx].cmdline);		// print which job is switched to foreground
		Kill(pid, SIGCONT);
		Update_queue(pid, Foreground);

		PID = 0;
		while (!PID) {						// wait for the termination of foregrounded job 
			if (foreground_check())			// only SIGINT or SIGTSTP can interrupt this situation
				Sigsuspend(&prev_one);
			else break;
		}
	}
	else { /* kill command */
		Kill(pid, SIGKILL);				// just kill the process and dequeue it
		Dequeue(pid);
	}
	Sigprocmask(SIG_SETMASK, &prev_one, NULL);
	return TRUE;
}

/* Jobs command implementation */
int jobs_command(void) {					
	for (int i = 1; i < MAXJOBS; i++) {
		if (queue[i].pid != 0) {
			printf("[%d] ", i);
			switch (queue[i].state) {		// print all background & suspended elems
			case Background:
				printf("running %s\n", queue[i].cmdline); break;
			case Stopped:
				printf("suspended %s\n", queue[i].cmdline); break;
			default: break;
			}
		}
	}
	return TRUE;
}

/* Procedure for 'cd' command */
int cd_command(char **argv) {			
	if (argv[1] == NULL || !strcmp(argv[1], "~"))	// go to the home dir
		return chdir(getenv("HOME")) + 1;
	if (chdir(argv[1]) < 0)							// go to the location that argv is pointing
		printf("No Such File or Directory\n");

	return TRUE;
}
/*** Subroutines for the Built-In-Commands End ***/



/***    Subroutines for the Signal Handling    ***/
/* SIGCHLD signal handler implementation */
void sigchld_handler(int sig) {				
	int olderrno = errno, status;
	sigset_t mask_all, prev_all;

	Sigfillset(&mask_all);					// mask any possible interrupting signals
	while ((PID = waitpid(-1, &status, WNOHANG)) > 0) {		// catch all terminated processes
		Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
											// if a process terminated normally, or SIGPIPE
		if (WIFEXITED(status) || status == SIGPIPE) // situation, then dequeue the job info!
			Dequeue(PID);					// if not, don't dequeue because it's suspended!

		Sigprocmask(SIG_SETMASK, &prev_all, NULL);
	}

	if (!((PID == 0) || (PID == -1 && errno == ECHILD)))
		Sio_error("waitpid error");
	errno = olderrno;
}

/* SIGINT Handler for 'Ctrl+C' situation */
void sigint_handler(int sig) {			
	int olderrno = errno;
	sigset_t mask_all, prev_one;
	Sigfillset(&mask_all);
	Sigprocmask(SIG_BLOCK, &mask_all, &prev_one);

	if (!read_flag) Sio_puts("\n> ");	// when just enter key pressed
	else Sio_puts("\n");	// read_flag means 'cmdline read' or 'during the evaluation'

	for (int i = 1; i < MAXJOBS; i++) {
		if (queue[i].state == Foreground) {		// send SIGKILL "only" to the foreground,
			Kill(queue[i].pid, SIGKILL);			// not to the background or suspended
			Dequeue(queue[i].pid);
		}
	}
	Sigprocmask(SIG_SETMASK, &prev_one, NULL);

	errno = olderrno;
}

/* SIGTSTP Handler for 'Ctrl+Z' situation */
void sigtstp_handler(int sig) {			
	int olderrno = errno;
	sigset_t mask_all, prev_one;
	Sigfillset(&mask_all);
	Sigprocmask(SIG_BLOCK, &mask_all, &prev_one);

	Sio_puts("\n");
	for (int i = 1; i < MAXJOBS; i++) {
		if (queue[i].state == Foreground) {		// send SIGSTOP "only" to the foreground,
			Kill(queue[i].pid, SIGSTOP);			// not to the background or suspended
			queue[i].state = Stopped;
		}
	}
	Sigprocmask(SIG_SETMASK, &prev_one, NULL);

	errno = olderrno;
}
/***  Subroutines for the Signal Handling End  ***/
/************** End of the Program ***************/