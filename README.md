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
program being managed. On SIGHUP, **god** recycles its log file making it
easy to integrate with logrotate. If SIGHUP is not supported by your program
**god** can handle it without forwarding the signal and making the program
immune to hangups using the *--nohup* command line option.

Go daemon is inspired by [twistd](http://twistedmatrix.com/documents/current/core/howto/basics.html#auto1),
but primarily for running servers written in the
[Go Programming Language](http://golang.org) that don't (or just can't)
care about daemonizing. It can also be used for running php, python and any
other type of long lived programs that need to be daemonized.

A typical command line look like this:

	god --nohup --logfile foo.log --pidfile foo.pid --user nobody --group nobody --rundir /opt/foo -- ./foobar --foobar-opts


## Why?

Like if there's not enough options out there: upstart, systemd, launchd,
daemontools, supervisord, runit, you name it. There's also utilities like
apache's logger, etc.

Go daemon aims at being as simple as possible to deploy and use, with
practically no dependencies (besides libc and libpthread). It is the very
minimum required for daemonizing programs while mixing well with other
subsystems like upstart and logrotate. All other utilities mentioned above
require configuration files and some times a complex installation with too
many dependencies.

It's ideal to use in docker images because it requires nothing and only
takes about 15k of disk space.


## Building

Go daemon is written in C and needs to be compiled. Debian and Ubuntu can
install the compiler and tools (make) with the following command:

	apt-get install build-essential

Then build and install it:

	make
	make install

The `god` command line tool should be ready to use.

Go daemon can be packaged for both [debian](debian/README.Debian) and
[rpm](rpm/README.md) based systems and has been tested on Ubuntu and CentOS.
