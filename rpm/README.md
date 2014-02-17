# Go daemon

This file contains documentation about how to build the rpm package from
the source code, available at http://github.com/fiorix/go-daemon.


## Building the rpm package

The following steps have been used to sucessfully build rpm packages on CentOS
and RHEL, and might work on other rpm-based distros.

Install dependencies:

	make deps

Build the rpm under ~/rpmbuild (will overwrite ~/.rpmmacros):

	make

Install:

	rpm -i ~/rpmbuild/RPMS/<arch>/go-daemon-<version>.<arch>.rpm

The RPM package can be distributed and installed on other systems.
