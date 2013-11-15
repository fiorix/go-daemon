// Copyright 2013 Alexandre Fiori
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
	"-e --env file       environment variables file\n"
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
static struct passwd *pwd = NULL;
static struct group *grp = NULL;
static pthread_mutex_t logger_mutex;

void daemon_main(int optind, char **argv, char **env);
void *logger_thread(void *cmdname);
void sighup(int signum);
void sigfwd(int signum);
char ** read_elvis(char *);

int main(int argc, char **argv) {
	char rundir[PATH_MAX];
	char user[64];
	char group[64];
	int foreground = 0;
    char envfilepath[PATH_MAX];
    char **env = NULL;

	memset(logfile, 0, sizeof logfile);
	memset(pidfile, 0, sizeof pidfile);
	memset(rundir, 0, sizeof rundir);
	memset(user, 0, sizeof user);
	memset(group, 0, sizeof group);
	memset(envfilepath, 0, sizeof envfilepath);

	static struct option opts[] = {
		{ "help",      no_argument,       NULL, 'h' },
		{ "version",   no_argument,       NULL, 'v' },
		{ "foreground", no_argument,      NULL, 'f' },
		{ "nohup",     no_argument,       NULL, 'n' },
		{ "logfile",   required_argument, NULL, 'l' },
		{ "pidfile",   required_argument, NULL, 'p' },
		{ "rundir",    required_argument, NULL, 'd' },
		{ "user",      required_argument, NULL, 'u' },
		{ "group",     required_argument, NULL, 'g' },
		{ "env",       optional_argument, NULL, 'e' },
		{ NULL, 0, NULL, 0 },
	};

	int ch;
	while (1) {
		ch = getopt_long(argc, argv, "l:p:r:u:g:hvfnse:", opts, NULL);
		if (ch == -1)
			break;

		switch (ch) {
			case 'v':
				printf("Go daemon v1.1\n");
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
			case 'e':
				strncpy(envfilepath, optarg, sizeof envfilepath - 1);
                env = read_elvis(envfilepath);
                if (!env) {
                    fprintf(stderr,"Error reading env file %s", envfilepath);
                    exit (-1);
                }
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

	if (*user != 0 && (pwd = getpwnam(user)) == NULL) {
		fprintf(stderr, "failed to switch to user %s: %s\n",
				user, strerror(errno));
		return 1;
	}

	if (*group != 0 && (grp = getgrnam(group)) == NULL) {
		fprintf(stderr, "failed to switch to group %s: %s\n",
				group, strerror(errno));
		return 1;
	}

	if (*logfile != 0 && (logfp = fopen(logfile, "a")) == NULL) {
		perror("failed to open logfile");
		return 1;
	}
	if (logfp)
		setvbuf(logfp, linebuf, _IOLBF, sizeof linebuf);

	if (*pidfile != 0 && (pidfp = fopen(pidfile, "w+")) == NULL) {
		perror("failed to open pidfile");
		return 1;
	}

	if (grp != NULL && setegid(grp->gr_gid) == -1) {
		fprintf(stderr, "failed to switch to group %s: %s\n",
				group, strerror(errno));
		return 1;
	}

	if (pwd != NULL && seteuid(pwd->pw_uid) == -1) {
		fprintf(stderr, "failed to switch to user %s: %s\n",
				user, strerror(errno));
		return 1;
	}

        if (foreground) {
                daemon_main(optind, argv, env);
        } else {
		// Daemonize.
		pid_t pid = fork();
		if (pid) {
			waitpid(pid, NULL, 0);
		} else if (!pid) {
			if ((pid = fork())) {
				exit(0);
			} else if (!pid) {
				daemon_main(optind, argv, env);
			} else {
				perror("fork");
				exit(1);
			}
		} else {
			perror("fork");
			exit(1);
		}
	}

	return 0;
}

void daemon_main(int optind, char **argv, char **env) {
	if (pidfp) {
		fprintf(pidfp, "%d\n", getpid());
		fclose(pidfp);
	}
	// Fwd all signals to the child, except SIGHUP.
	int signum;
	for (signum = 1; signum < 33; signum++) {
		if (signal(signum, sigfwd) == SIG_IGN)
			signal(signum, SIG_IGN);
	}
	signal(SIGHUP, sighup);
	pipe(logfd);
	if ((childpid = fork())) {
		close(logfd[1]);
		pthread_t logth;
		pthread_create(&logth, NULL, logger_thread, argv[optind]);
		waitpid(childpid, NULL, 0);
		pthread_join(logth, NULL);
	} else if (!childpid) {
		close(logfd[0]);
		close(0);
		close(1);
		close(2);
		dup2(logfd[1], 1);
		dup2(logfd[1], 2);
		if (!env) {
            #ifdef __APPLE__
                execve(argv[optind], argv + optind, env);
            #else
                execvpe(argv[optind], argv + optind, env);
            #endif
        } else {
		    execvp(argv[optind], argv + optind);
		}
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

	return NULL;
}

void sighup(int signum) {
	if (pwd != NULL) {
		seteuid(getuid());
	}
	if (grp != NULL) {
		setegid(getgid());
	}
	pthread_mutex_lock(&logger_mutex);
	if (logfp) {
		fclose(logfp);
		logfp = fopen(logfile, "a");
		setvbuf(logfp, linebuf, _IOLBF, sizeof linebuf);
	}
	if (grp != NULL) {
		setegid(grp->gr_gid);
	}
	if (pwd != NULL) {
		seteuid(pwd->pw_uid);
	}
	pthread_mutex_unlock(&logger_mutex);
	if (!nohup && childpid) // nonohup :~
		kill(childpid, signum);
}

void sigfwd(int signum) {
	if (childpid)
		kill(childpid, signum);
}

char **read_elvis(char *file){
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    char **envp = NULL;
    int current_line = 1;


    fp = fopen(file, "r");
    if (fp == NULL) return NULL;
    printf("Reading env from %s\n", file);

    while ((read = getline(&line, &len, fp)) != -1) {
        envp = realloc(envp, current_line * sizeof(char *));
        if (envp == NULL) {
            perror("realloc");
            return NULL;
        }
        envp[current_line -1 ] = strdup(line);
        current_line ++;
    }
    envp = realloc(envp, current_line * sizeof(char *));
    envp[current_line - 1] = NULL;

    if (line) free(line);
    for (int a=0; envp[a] != NULL; a++){
        fprintf(stderr, "%s", envp[a]);
    }
    return envp;
}
