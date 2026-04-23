// Copyright 2013-2026 Alexandre Fiori
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _GNU_SOURCE

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

#ifdef __linux__
#include <sys/prctl.h>
#endif

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
	"\nThe program's output goes to a blackhole if no logfile is set.\n"
	"Log files are recycled on SIGHUP.\n"
	);
}

static int nohup = 0;
static int logfd[2]; // pipe
static pid_t childpid = 0;
static FILE *logfp = NULL;
static FILE *pidfp = NULL;
static char logfile[PATH_MAX];
static char pidfile[PATH_MAX];
static char linebuf[1024];
static pthread_mutex_t logger_mutex = PTHREAD_MUTEX_INITIALIZER;
static sigset_t fwd_mask;

void daemon_main(int foreground, int optind, char **argv);
void signal_setup(sigset_t *old_sigmask);
void *signal_thread(void *arg);
void *logger_thread(void *arg);
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
		ch = getopt_long(argc, argv, "l:p:r:u:g:hvfn", opts, NULL);
		if (ch == -1)
			break;

		switch (ch) {
			case 'h':
				usage();
				return 0;
			case 'v':
				printf("Go daemon %s\n", VERSION);
				printf("https://github.com/fiorix/go-daemon\n");
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
				return 1;
		}
	}

	// utility is expected to be argv's leftovers.
	if (optind >= argc) {
		usage();
		return 1;
	}

	if (*rundir != 0 && chdir(rundir) == -1) {
		perror("failed to switch to rundir");
		return 1;
	}

	struct passwd *pwd = NULL;
	if (*user) {
		errno = 0;
		if (!(pwd = getpwnam(user))) {
			if (errno == 0)
				fprintf(stderr, "user not found: %s\n", user);
			else
				fprintf(stderr, "failed to get user %s: %s\n",
						user, strerror(errno));
			return 1;
		}
	}

	struct group *grp = NULL;
	if (*group) {
		errno = 0;
		if (!(grp = getgrnam(group))) {
			if (errno == 0)
				fprintf(stderr, "group not found: %s\n", group);
			else
				fprintf(stderr, "failed to get group %s: %s\n",
						group, strerror(errno));
			return 1;
		}
	}

	// Drop privileges. If --user is set without --group, use the user's
	// primary group so we don't silently keep the caller's (often root) GID.
	if (pwd || grp) {
		gid_t tgid = grp ? grp->gr_gid : pwd->pw_gid;
		if (setgroups(1, &tgid) == -1) {
			fprintf(stderr, "failed to drop supplementary groups: %s\n",
					strerror(errno));
			return 1;
		}
		if (setregid(tgid, tgid) == -1) {
			fprintf(stderr, "failed to set gid %u: %s\n",
					(unsigned)tgid, strerror(errno));
			return 1;
		}
	}
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

	if (*pidfile) {
		char pidtmp[PATH_MAX + 8];
		int n = snprintf(pidtmp, sizeof pidtmp, "%s.tmp", pidfile);
		if (n < 0 || (size_t)n >= sizeof pidtmp) {
			fprintf(stderr, "pidfile path too long\n");
			return 1;
		}
		if ((pidfp = fopen(pidtmp, "w")) == NULL) {
			perror("failed to open pidfile");
			return 1;
		}
	}

	char *abspath;
	if (!(abspath = exec_abspath(argv[optind]))) {
		perror(argv[optind]);
		return 1;
	}
	argv[optind] = abspath;

	if (foreground) {
		daemon_main(1, optind, argv);
		return 0;
	}

	// Daemonize.
	pid_t pid = fork();
	if (pid > 0) {
		waitpid(pid, NULL, 0);
	} else if (!pid) {
		if (setsid() < 0) {
			perror("setsid");
			exit(1);
		}
		if ((pid = fork()) > 0) {
			exit(0);
		} else if (!pid) {
			daemon_main(0, optind, argv);
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

static void write_execerr(const char *prog, int err) {
	FILE *fp = NULL;
	int close_fp = 0;
	if (*logfile) {
		fp = fopen(logfile, "a");
		close_fp = 1;
	}
	if (!fp)
		fp = stderr; // only useful in foreground; /dev/null otherwise
	fprintf(fp, "%s: %s\n", prog, strerror(err));
	if (close_fp)
		fclose(fp);
}

void daemon_main(int foreground, int optind, char **argv) {
	// Commit the pidfile atomically: write to <pidfile>.tmp, fsync, rename.
	if (*pidfile && pidfp) {
		char pidtmp[PATH_MAX + 8];
		snprintf(pidtmp, sizeof pidtmp, "%s.tmp", pidfile);
		fprintf(pidfp, "%d\n", getpid());
		fflush(pidfp);
		fsync(fileno(pidfp));
		fclose(pidfp);
		pidfp = NULL;
		if (rename(pidtmp, pidfile) != 0)
			unlink(pidtmp);
	}

	sigset_t old_sigmask;
	signal_setup(&old_sigmask);

	int errfd[2];
	if (pipe2(errfd, O_CLOEXEC) < 0) {
		perror("pipe");
		if (*pidfile) unlink(pidfile);
		exit(1);
	}
	if (pipe2(logfd, O_CLOEXEC) < 0) {
		perror("pipe");
		close(errfd[0]); close(errfd[1]);
		if (*pidfile) unlink(pidfile);
		exit(1);
	}

	childpid = fork();
	if (childpid > 0) {
		// Supervisor.
		close(errfd[1]);
		close(logfd[1]);

		if (!foreground) {
			int devnull = open("/dev/null", O_RDWR);
			if (devnull >= 0) {
				dup2(devnull, 0);
				dup2(devnull, 1);
				dup2(devnull, 2);
				if (devnull > 2) close(devnull);
			}
		}

		pthread_t sigth;
		if (pthread_create(&sigth, NULL, signal_thread, NULL) != 0) {
			perror("pthread_create signal");
			kill(-childpid, SIGKILL);
			waitpid(childpid, NULL, 0);
			if (*pidfile) unlink(pidfile);
			exit(1);
		}
		pthread_detach(sigth);

		pthread_t logth;
		int have_logth = (pthread_create(&logth, NULL, logger_thread, NULL) == 0);
		if (!have_logth) {
			// Log thread failed: drop the read end so the child's
			// writes fail fast instead of blocking on a full pipe.
			close(logfd[0]);
		}

		int execerr = 0;
		ssize_t nerr = read(errfd[0], &execerr, sizeof execerr);
		close(errfd[0]);

		waitpid(childpid, NULL, 0);
		if (have_logth)
			pthread_join(logth, NULL);

		if (nerr == (ssize_t)sizeof execerr)
			write_execerr(argv[optind], execerr);

		if (*pidfile) unlink(pidfile);
		return;
	}
	if (childpid == 0) {
		// Child: restore signal mask, become process-group leader,
		// ask the kernel to signal us if the supervisor dies.
		int rc = pthread_sigmask(SIG_SETMASK, &old_sigmask, NULL);
		if (rc != 0) {
			(void)write(errfd[1], &rc, sizeof rc);
			_exit(127);
		}
		if (setpgid(0, 0) != 0) {
			int err = errno;
			(void)write(errfd[1], &err, sizeof err);
			_exit(127);
		}
#ifdef __linux__
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		// Close the race: if the supervisor already exited, we won't
		// receive the pdeathsig; bail out so we don't orphan.
		if (getppid() == 1)
			_exit(0);
#endif
		close(errfd[0]);
		close(logfd[0]);
		if (dup2(logfd[1], 1) < 0 || dup2(logfd[1], 2) < 0) {
			int err = errno;
			(void)write(errfd[1], &err, sizeof err);
			_exit(127);
		}
		close(logfd[1]);
		if (logfp) {
			fclose(logfp);
			logfp = NULL;
		}

		execvp(argv[optind], argv + optind);
		int err = errno;
		(void)write(errfd[1], &err, sizeof err);
		close(errfd[1]);
		_exit(127);
	}
	perror("fork");
	close(errfd[0]); close(errfd[1]);
	close(logfd[0]); close(logfd[1]);
	if (*pidfile) unlink(pidfile);
	exit(1);
}

// signal_setup blocks the signals we want to forward to the child so they
// can be consumed by signal_thread via sigwait. SIGPIPE is blocked so a
// broken logfile write doesn't kill the supervisor. Signals not listed
// here keep their default disposition.
void signal_setup(sigset_t *old_sigmask) {
	static const int fwd_signals[] = {
		SIGHUP, SIGINT, SIGQUIT, SIGTERM,
		SIGUSR1, SIGUSR2, SIGTSTP, SIGCONT,
	};
	sigemptyset(&fwd_mask);
	for (size_t i = 0; i < sizeof fwd_signals / sizeof fwd_signals[0]; i++)
		sigaddset(&fwd_mask, fwd_signals[i]);

	sigset_t block_mask = fwd_mask;
	sigaddset(&block_mask, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &block_mask, old_sigmask) != 0) {
		perror("pthread_sigmask");
		exit(1);
	}
}

void *signal_thread(void *arg) {
	(void)arg;
	while (1) {
		int signum;
		if (sigwait(&fwd_mask, &signum) != 0) {
			perror("sigwait");
			return NULL;
		}

		if (signum == SIGHUP) {
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
			if (nohup)
				continue;
		}
		if (childpid > 0)
			kill(-childpid, signum);
	}
}

void *logger_thread(void *arg) {
	(void)arg;
	char buf[4096];

	// read() returns 0 when the child closes its end of the pipe (dies or
	// fails to exec after we duped logfd[1] to its stdout/stderr). That
	// terminates this thread and the supervisor moves on.
	for (;;) {
		ssize_t n = read(logfd[0], buf, sizeof buf);
		if (n <= 0)
			break;
		pthread_mutex_lock(&logger_mutex);
		if (logfp)
			fwrite(buf, 1, (size_t)n, logfp);
		pthread_mutex_unlock(&logger_mutex);
	}
	pthread_mutex_lock(&logger_mutex);
	if (logfp) {
		fflush(logfp);
		fclose(logfp);
		logfp = NULL;
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

	if (strchr(filename, '/')) {
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
