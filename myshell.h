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

/* $begin myshell.h */
#ifndef __MYSHELL_H__
#define __MYSHELL_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Constants */
#define	MAXLINE	 8192	/* Max text line length */
#define MAXARGS	 128	/* Max Arguments number */
#define TRUE	 1		/* for Readability */
#define FALSE 	 0		/* for Readability */
#define MAXJOBS	 50		/* Job Queue Size */

/* Types for Job Queue */
typedef enum { Invalid, Foreground, Background, Stopped }State;

typedef struct {				/* structure for Job Queue */
	int idx;					// index of an element in the job queue
	pid_t pid;					// pid of an element in the job queue
	State state;				// state of an element in the job queue
	char cmdline[MAXLINE];		// command of an element in the job queue
	int last_idx;				// is an element the last element of queue?
}Job;

Job queue[MAXJOBS];				// array based Job Queue
int queue_size;
int queue_last;					// the index of the last element in the Job queue

/* Our own error-handling functions */
void unix_error(char *msg);
void app_error(char *msg);

/* Process control wrappers */
pid_t Fork(void);
void Execvp(const char *filename, char *const argv[]);
pid_t Wait(int *status);
pid_t Waitpid(pid_t pid, int *iptr, int options);
void Kill(pid_t pid, int signum);

/* Signal wrappers */
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Sigemptyset(sigset_t *set);
void Sigfillset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
int Sigsuspend(const sigset_t *set);
void Sigset_setting(sigset_t *mask_all, sigset_t *mask_one, sigset_t *mask_two);

/* Sio (Signal-safe I/O) routines */
ssize_t sio_puts(char s[]);
ssize_t sio_putl(long v);
void sio_error(char s[]);

/* Sio wrappers */
ssize_t Sio_puts(char s[]);
ssize_t Sio_putl(long v);
ssize_t Sio_printjob(int idx, char *cmdline);
ssize_t Sio_printBGjob(int idx, pid_t pid);
void Sio_error(char s[]);

/* Unix I/O wrappers */
void Close(int fd);
int Dup2(int fd1, int fd2);
int Pipe(int *fd);

/* Standard I/O wrappers */
char *Fgets(char *ptr, int n, FILE *stream);

/* Job Queue */
void init_queue(void);
void Enqueue(pid_t pid, State state, char *cmdline);
void Dequeue(pid_t pid);
void Update_queue(pid_t pid, State state);
Job *Search_queue(pid_t pid);


/**************************
 * Error-handling functions
 **************************/
void unix_error(char *msg) /* Unix-style error */
{
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(0);
}

void app_error(char *msg) /* Application error */
{
	fprintf(stderr, "%s\n", msg);
	exit(0);
}


/*********************************************
 * Wrappers for Unix process control functions
 ********************************************/
pid_t Fork(void)
{
	pid_t pid;

	if ((pid = fork()) < 0)
		unix_error("Fork error");
	return pid;
}

void Execvp(const char *filename, char *const argv[])
{
	if (execvp(filename, argv) < 0) {
		printf("%s: Command not found.\n", filename);
		exit(0);
	}
}

pid_t Wait(int *status)
{
	pid_t pid;

	if ((pid = wait(status)) < 0)
		unix_error("Wait error");
	return pid;
}

pid_t Waitpid(pid_t pid, int *iptr, int options)
{
	pid_t retpid;

	if ((retpid = waitpid(pid, iptr, options)) < 0)
		Sio_puts("Waitpid error");

	return(retpid);
}

void Kill(pid_t pid, int signum)
{
	int rc;

	if ((rc = kill(pid, signum)) < 0)
		unix_error("Kill error");
}


/************************************
 * Wrappers for Unix signal functions
 ***********************************/
handler_t *Signal(int signum, handler_t *handler)
{
	struct sigaction action, old_action;

	action.sa_handler = handler;
	sigemptyset(&action.sa_mask); /* Block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* Restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}

void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	if (sigprocmask(how, set, oldset) < 0)
		unix_error("Sigprocmask error");
	return;
}

void Sigemptyset(sigset_t *set)
{
	if (sigemptyset(set) < 0)
		unix_error("Sigemptyset error");
	return;
}

void Sigfillset(sigset_t *set)
{
	if (sigfillset(set) < 0)
		unix_error("Sigfillset error");
	return;
}

void Sigaddset(sigset_t *set, int signum)
{
	if (sigaddset(set, signum) < 0)
		unix_error("Sigaddset error");
	return;
}

int Sigsuspend(const sigset_t *set)
{
	int rc = sigsuspend(set); /* always returns -1 */
	if (errno != EINTR)
		unix_error("Sigsuspend error");
	return rc;
}

void Sigset_setting(sigset_t *mask_all, sigset_t *mask_one, sigset_t *mask_two) {
	Sigfillset(mask_all);
	Sigemptyset(mask_one);
	Sigemptyset(mask_two);
	Sigaddset(mask_one, SIGCHLD);
	Sigaddset(mask_one, SIGINT);
	Sigaddset(mask_one, SIGTSTP);
	Sigaddset(mask_two, SIGINT);
	Sigaddset(mask_two, SIGTSTP);
}


/*************************************************************
 * The Sio (Signal-safe I/O) package - simple reentrant output
 * functions that are safe for signal handlers.
 *************************************************************/

 /* Private sio functions */
static void sio_reverse(char s[])
{
	int c, i, j;

	for (i = 0, j = strlen(s) - 1; i < j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}

static void sio_ltoa(long v, char s[], int b)
{
	int c, i = 0;

	do {
		s[i++] = ((c = (v % b)) < 10) ? c + '0' : c - 10 + 'a';
	} while ((v /= b) > 0);
	s[i] = '\0';
	sio_reverse(s);
}

static size_t sio_strlen(char s[])
{
	int i = 0;

	while (s[i] != '\0')
		++i;
	return i;
}

/* Public Sio functions */
ssize_t sio_puts(char s[]) /* Put string */
{
	return write(STDOUT_FILENO, s, sio_strlen(s));
}

ssize_t sio_putl(long v) /* Put long */
{
	char s[128];

	sio_ltoa(v, s, 10); /* Based on K&R itoa() */
	return sio_puts(s);
}

void sio_error(char s[]) /* Put error message and exit */
{
	sio_puts(s);
	_exit(1);
}


/*******************************
 * Wrappers for the SIO routines
 ******************************/
ssize_t Sio_putl(long v)
{
	ssize_t n;

	if ((n = sio_putl(v)) < 0)
		sio_error("Sio_putl error");
	return n;
}

ssize_t Sio_puts(char s[])
{
	ssize_t n;

	if ((n = sio_puts(s)) < 0)
		sio_error("Sio_puts error");
	return n;
}

ssize_t Sio_printjob(int idx, char *cmdline) {
	Sio_puts("["); Sio_putl((long)idx); Sio_puts("] running ");
	Sio_puts(cmdline); Sio_puts("\n");
}

ssize_t Sio_printBGjob(int idx, pid_t pid) {
	Sio_puts("["); Sio_putl((long)idx);
	Sio_puts("] "); Sio_putl((long)pid); Sio_puts("\n");
}

void Sio_error(char s[])
{
	sio_error(s);
}


/********************************
 * Wrappers for Unix I/O routines
 ********************************/
void Close(int fd)
{
	int rc;

	if ((rc = close(fd)) < 0)
		unix_error("Close error");
}

int Dup2(int fd1, int fd2)
{
	int rc;

	if ((rc = dup2(fd1, fd2)) < 0)
		unix_error("Dup2 error");
	return rc;
}

int Pipe(int *fd)
{
	if (pipe(fd) < 0)
		unix_error("Pipe error");
}


/******************************************
 * Wrappers for the Standard I/O functions.
 ******************************************/
char *Fgets(char *ptr, int n, FILE *stream)
{
	char *rptr;
	
	if (((rptr = fgets(ptr, n, stream)) == NULL) && ferror(stream))
		app_error("Fgets error");

	return rptr;
}


/******************************************
 * Functions for implementing the Job Queue.
 ******************************************/
void init_queue(void) {						/* Job queue initialization function */
	for (int i = 1; i < MAXJOBS; i++) {
		queue[i].idx = 0;
		queue[i].pid = 0;
		queue[i].state = Invalid;
		queue[i].cmdline[0] = '\0';
		queue[i].last_idx = FALSE;
	}
}

void Enqueue(pid_t pid, State state, char *cmdline) {	/* Job queue push function */
	int i, k = 1;

	if (queue_size == 0) {				// if job queue is empty, then push to the front
		i = 1;
		queue_last = i;
	}
	else {								// if queue isn't empty, then push to next position
		i = ++queue_last;				// of the current last element
		if (i >= MAXJOBS)
			unix_error("Enqueue error");	// if queue is full, then abort with error msg

		queue[i - 1].last_idx = FALSE;		// update the former last element as non-last
	}

	queue[i].idx = i;
	queue[i].pid = pid;
	queue[i].state = state;

	queue[i].cmdline[0] = cmdline[0];
	for (int j = 1; cmdline[j]; j++) {						// special string copy routine
		if ((cmdline[j] == ' ' && cmdline[j - 1] == ' ')	// ignore the consequtive spaces
			|| cmdline[j] == '&' || cmdline[j] == '\n') continue;	// and ampersand symbol

		queue[i].cmdline[k++] = cmdline[j];
	}
	queue[i].cmdline[k] = '\0';

	queue[i].last_idx = TRUE;			// pushed element is now the last element of queue
	queue_size++;
}

void Dequeue(pid_t pid) {					/* Job queue pop function */
	for (int i = 1; i < MAXJOBS; i++) {
		if (queue[i].state != Invalid && queue[i].pid == pid) {
			queue[i].idx = 0;
			queue[i].pid = 0;
			queue[i].state = Invalid;		// make the element as Invalid
			strcpy(queue[i].cmdline, "");

			if (queue[i].last_idx)			// if deleted element was the last element,
				queue_last--;				// then decrement the position of last element

			queue_size--;
			return;
		}
	}
}

void Update_queue(pid_t pid, State state) {	/* Job queue update function */
	for (int i = 1; i < MAXJOBS; i++) {
		if (queue[i].pid == pid) {
			queue[i].state = state;			// change the state of element
			return;
		}
	}
	unix_error("Update_queue error");
}

Job *Search_queue(pid_t pid) {				/* Job queue search function */
	for (int i = 1; i < MAXJOBS; i++) {
		if (queue[i].pid == pid)
			return &(queue[i]);				// return the address of element
	}
	return NULL;
}

#endif