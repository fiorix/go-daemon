# Go daemon

Go daemon (or just **god**) is a command line tool to "daemonize" programs
that originally only run in foreground and write logs to the console.

Go daemon can turn these programs into daemons, managing essential aspects of
their execution. The process of making a program become a daemon has very
peculiar steps and can be done outside the code. This is what **god** is for.

It executes a program for you doing things that daemons do: switch to another
user and group, switch the directory of execution, detach from the terminal
and create a pid file. While the program runs, it consumes its output
(stdout and stderr) and write to a log file.

It also handles all signals (SIGINT, SIGTERM, etc) and forward them to the
child process. On SIGHUP, **god** recycles its log file making it easy to
integrate with logrotate.

The command line look like this:

	god --nohup --logfile foo.log --pidfile foo.pid --user nobody --group nobody --rundir /opt/foo -- ./my-daemon --my-daemon-opts

Go daemon is inspired by [twistd](http://twistedmatrix.com/documents/current/core/howto/basics.html#auto1),
but primarily for running servers written in the
[Go Programming Language](http://golang.org) that don't care about daemonizing.
It can also be used for running php, python and any other type of long lived
programs that need to be daemonized.

## Why?

Like if there's not enough options out there: upstart, systemd, launchd,
daemontools, supervisord, etc... and the list goes on.

Go daemon aims at the very minimum for daemonizing while supporting other
subsystems like logrotate. All others mentioned above require configuration
files and some times a complex installation. But **god** mix well with them,
for example upstart.

## Building

Go daemon is written in C and needs to be compiled. Debian and Ubuntu can
install the compiler and tools (make) with the following command:

	apt-get install build-essential

Once installed just type **make** to build it:

	$ make
	cc god.c -o god -lpthread

The `god` command line tool should be ready to be used.
