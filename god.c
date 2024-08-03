// Copyright 2013-2016 Alexandre Fiori
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#ifndef VERSION
#define VERSION "tip"
#endif

void usage() {
	printf(
	"Use: god [options] [--] program [arguments]\n"
	"Options:\n"
	"-h --help           show this help and exit\n"
	"-v --version        show version and exit\n"
	"-f --foreground     run in foreground\n"
	"-n --nohup          make the program immune to SIGHUP\n"
	"-l --logfile FILE   write the program's stdout and stderr to FILE\n"
	"-p --pidfile FILE   write pid to FILE\n"
	"-r --rundir DIR     switch to DIR before executing the program\n"
	"-u --user USER      switch to USER before executing the program\n"
	"-g --group GROUP    switch to GROUP before executing the program\n"
	"\nThe program's output go to a blackhole if no logfile is set.\n"
	"Log files are recycled on SIGHUP.\n"
	);
	exit(1);
}

static int nohup = 0;
static int logfd[2]; // pipe
static pid_t childpid = 0;
static FILE *logfp = NULL;
static FILE *pidfp = NULL;
static char logfile[PATH_MAX];
static char pidfile[PATH_MAX];
static char linebuf[1024];
static pthread_mutex_t logger_mutex;

void daemon_main(int optind, char **argv);
void signal_init(sigset_t* old_sigmask);
void *signal_thread(void* arg);
void *logger_thread(void *cmdname);
char *exec_abspath(char *filename);

int main(int argc, char **argv) {
	char rundir[PATH_MAX];
	char user[64];
	char group[64];
	int foreground = 0;

	memset(logfile, 0, sizeof logfile);
	memset(pidfile, 0, sizeof pidfile);
	memset(rundir, 0, sizeof rundir);
	memset(user, 0, sizeof user);
	memset(group, 0, sizeof group);

	static struct option opts[] = {
		{ "help",	no_argument,		NULL, 'h' },
		{ "version",	no_argument,		NULL, 'v' },
		{ "foreground",	no_argument,		NULL, 'f' },
		{ "nohup",	no_argument,		NULL, 'n' },
		{ "logfile",	required_argument,	NULL, 'l' },
		{ "pidfile",	required_argument,	NULL, 'p' },
		{ "rundir",	required_argument,	NULL, 'r' },
		{ "user",	required_argument,	NULL, 'u' },
		{ "group",	required_argument,	NULL, 'g' },
		{ NULL, 0, NULL, 0 },
	};

	int ch;
	while (1) {
		ch = getopt_long(argc, argv, "l:p:r:u:g:hvfns", opts, NULL);
		if (ch == -1)
			break;

		switch (ch) {
			case 'v':
				printf("Go daemon %s\n", VERSION);
				printf("http://github.com/fiorix/go-daemon\n");
				return 0;
			case 'f':
				foreground = 1;
				break;
			case 'n':
				nohup = 1;
				break;
			case 'l':
				strncpy(logfile, optarg, sizeof logfile - 1);
				break;
			case 'p':
				strncpy(pidfile, optarg, sizeof pidfile - 1);
				break;
			case 'r':
				strncpy(rundir, optarg, sizeof rundir - 1);
				break;
			case 'u':
				strncpy(user, optarg, sizeof user - 1);
				break;
			case 'g':
				strncpy(group, optarg, sizeof group - 1);
				break;
			default:
				usage();
		}
	}

	// utility is expected to be argv's leftovers.
	if (optind >= argc)
		usage();

	if (*rundir != 0 && chdir(rundir) == -1) {
		perror("failed to switch to rundir");
		return 1;
	}

	struct passwd *pwd = NULL;
	if ((*user && !(pwd = getpwnam(user)))) {
		fprintf(stderr, "failed to get user %s: %s\n",
				user, strerror(errno));
		return 1;
	}

	struct group *grp = NULL;
	if ((*group && !(grp = getgrnam(group)))) {
		fprintf(stderr, "failed to get group %s: %s\n",
				group, strerror(errno));
		return 1;
	}

	if (grp && setregid(grp->gr_gid, grp->gr_gid) == -1) {
		fprintf(stderr, "failed to switch to group %s: %s\n",
				group, strerror(errno));
		return 1;
	}

	const gid_t gid = getgid();
	setgroups(1, &gid);

	if (pwd && setreuid(pwd->pw_uid, pwd->pw_uid) == -1) {
		fprintf(stderr, "failed to switch to user %s: %s\n",
				user, strerror(errno));
		return 1;
	}

	if (*logfile && (logfp = fopen(logfile, "a")) == NULL) {
		perror("failed to open logfile");
		return 1;
	}
	if (logfp)
		setvbuf(logfp, linebuf, _IOLBF, sizeof linebuf);

	if (*pidfile && (pidfp = fopen(pidfile, "w+")) == NULL) {
		perror("failed to open pidfile");
		return 1;
	}

	char *abspath;
	if (!(abspath = exec_abspath(argv[optind]))) {
		perror(argv[optind]);
		return 1;
	}
	argv[optind] = abspath;

        if (foreground) {
                daemon_main(optind, argv);
		return 0;
	}

	// Daemonize.
	pid_t pid = fork();
	if (pid) {
		waitpid(pid, NULL, 0);
	} else if (!pid) {
		if ((pid = fork())) {
			exit(0);
		} else if (!pid) {
			daemon_main(optind, argv);
		} else {
			perror("fork");
			exit(1);
		}
	} else {
		perror("fork");
		exit(1);
	}

	return 0;
}

void daemon_main(int optind, char **argv) {
	if (pidfp) {
		fprintf(pidfp, "%d\n", getpid());
		fclose(pidfp);
	}

	sigset_t old_sigmask;
	signal_init(&old_sigmask);
	pipe(logfd);
	if ((childpid = fork())) {
		close(0);
		close(1);
		close(2);
		close(logfd[1]);
		pthread_t logth;
		pthread_create(&logth, NULL, logger_thread, argv[optind]);
		waitpid(childpid, NULL, 0);
		pthread_join(logth, NULL);
	} else if (!childpid) {
		pthread_sigmask(SIG_SETMASK, &old_sigmask, NULL);
		close(logfd[0]);
		dup2(logfd[1], 1);
		dup2(logfd[1], 2);
		if (logfp) {
			fclose(logfp);
		}
		execvp(argv[optind], argv + optind);
		printf("\x1b%s", strerror(errno));
		fflush(stdout);
		close(logfd[1]);
		close(1);
		close(2);
	} else {
		perror("fork");
		exit(1);
	}
	if (pidfp)
		unlink(pidfile);
}

void signal_init(sigset_t* old_sigmask) {
	sigset_t new_sigmask;
	if(sigfillset(&new_sigmask)) {
		perror("sigfillset");
		exit(1);
	}

	size_t i;
	int no_fwd_signals[] = {SIGFPE, SIGILL, SIGSEGV, SIGBUS, SIGABRT, SIGTRAP, SIGSYS};
	for (i = 0; i < (sizeof(no_fwd_signals)/sizeof(no_fwd_signals[0])); i++) {
		if(sigdelset(&new_sigmask, no_fwd_signals[i])) {
			perror("sigdelset");
			exit(1);
		}
	}

	if(pthread_sigmask(SIG_SETMASK, &new_sigmask, old_sigmask)) {
		perror("pthread_sigmask");
		exit(1);
	}

	pthread_t signalth;
	if(pthread_create(&signalth, NULL, signal_thread, &new_sigmask)) {
		perror("pthread_create");
		exit(1);
	}
}

void *signal_thread(void* arg) {
	sigset_t *sigmask = (sigset_t *)arg;

	while (1) {
		int signum;
		if(sigwait(sigmask, &signum)) {
			perror("sigwait");
			exit(1);
		}

		if(signum == SIGHUP) {
			pthread_mutex_lock(&logger_mutex);
			if (logfp) {
				FILE *fp = fopen(logfile, "a");
				if (fp != NULL) {
					fclose(logfp);
					logfp = fp;
					setvbuf(logfp, linebuf, _IOLBF, sizeof linebuf);
				}
			}
			pthread_mutex_unlock(&logger_mutex);
			if (!nohup && childpid) // nonohup :~
				kill(childpid, signum);
		} else {
			if (childpid)
				kill(childpid, signum);
		}
	}
}

void *logger_thread(void *cmdname) {
	int n;
	char buf[4096];
	int has_read = 0;

	while(1) {
		// read() will fail when the child process fails
		// to execute or dies, and closes its terminal.
		// This is what terminates this thread and therefore
		// the main thread can move along.
		n = read(logfd[0], buf, sizeof buf);
		if (n <= 0)
			break;

		buf[n] = 0;
		if (!has_read) {
			has_read = 1;
			if (*buf == '\x1b') {
				char *p = buf;
				printf("%s: %s\n", (char *) cmdname, ++p);
				close(logfd[0]);
				break;
			}
		}

		pthread_mutex_lock(&logger_mutex);
		if (logfp) {
			fwrite(buf, 1, n, logfp);
			//fflush(logfp);
		}
		pthread_mutex_unlock(&logger_mutex);
	}
	pthread_mutex_lock(&logger_mutex);
	if (logfp) {
		fflush(logfp);
		fclose(logfp);
	}
	pthread_mutex_unlock(&logger_mutex);
	return NULL;
}

int exec_ok(char *filename) {
	struct stat st;
	if (stat(filename, &st) < 0) {
		errno = ENOENT;
		return 0;
	}
	if (S_ISDIR(st.st_mode)) {
		errno = EISDIR;
		return 0;
	}
	if (!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
		errno = EACCES;
		return 0;
	}
	return 1;
}

// exec_abspath returns the absolute path of the given file name by
// looking it up in the current directory and the directories listed
// in the PATH environment variable, only if the file is executable.
// The returned pointer points to a static buffer that is reused on
// subsequent calls. In case the file does not exist, or does not have
// executable permissions, returns NULL and errno is set accordingly.
char *exec_abspath(char *filename) {
	static char abspath[PATH_MAX];

	if (!filename) {
		errno = ENOENT;
		return NULL;
	}

	if (*filename == '.' || *filename == '/') {
		switch (exec_ok(filename)) {
		case  1: return realpath(filename, abspath);
		default: return NULL;
		}
	}

	char *path = getenv("PATH");
	if (!path) return NULL;

	char *v[(PATH_MAX/2)], **paths = v;
	char *p = strdup(path);
	path = p;

	while (*p) {
		while (*p && *p == ':') *p++ = 0;
		if (*p) *(paths++) = p;
		while (*p && *p != ':') p++;
	}
	*paths = NULL;

	int l, fnlen = strlen(filename);
	for (paths = v; *paths; paths++) {
		l = strlen(*paths) + fnlen + 2;
		if (l > sizeof(abspath)) continue;
		snprintf(abspath, l, "%s/%s", *paths, filename);
		if (exec_ok(abspath)) {
			free(path);
			return abspath;
		}
	}
	free(path);
	return NULL;
}
