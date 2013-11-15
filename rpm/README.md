# Go daemon

This file contains documentation about how to build the rpm package from
the source code, available at http://github.com/fiorix/go-daemon.


## Building the rpm package

The following steps have been used to sucessfully build rpm packages on CentOS
and RHEL, and might work on other rpm-based distros.

Install dependencies:

	yum install gcc make rpm-build redhat-rpm-config

Create directories for building under your home:

	mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	echo '%_topdir %(echo $HOME)/rpmbuild' > ~/.rpmmacros

Copy the source code and spec file to the building environment:

	cp -r go-daemon go-daemon-1.1
	tar czvf ~/rpmbuild/SOURCES/go-daemon-1.1.tar.gz go-daemon-1.1
	cp go-daemon/rpm/go-daemon.spec ~/rpmbuild/SPECS

Build the package:

	cd ~/rpmbuild/SPECS
	rpmbuild -ba go-daemon.spec

Install it:

	cd ~/rpmbuild/RPMS/<arch>
	rpm -i go-daemon-1.1-1.<arch>.rpm

The package can be distributed and installed on other systems.
