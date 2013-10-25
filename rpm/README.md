# Go daemon

This file contains documentation about how to build the rpm package from
the source code, available at http://github.com/fiorix/go-daemon.


## Building the rpm package

The following steps have been used to sucessfully build rpm packages on CentOS
and might work on other rpm-based distros.

1. Install dependencies

	yum install gcc make rpm-build redhat-rpm-config

2. Create directories for building under your home

	mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	echo '%_topdir %(echo $HOME)/rpmbuild' > ~/.rpmmacros

3. Copy the source code and spec file to the building environment

	cp -r go-daemon go-daemon-1.0
	tar czvf ~/rpmbuild/SOURCES/go-daemon-1.0.tar.gz go-daemon-1.0
	cp go-daemon/rpm/go-daemon.spec ~/rpmbuild/SPECS

4. Build the package

	cd ~/rpmbuild/SPECS
	rpmbuild -ba go-daemon.spec

5. Install it

	cd ~/rpmbuild/RPMS/<arch>
	rpm -i go-daemon-1.0-1.<arch>.rpm

The package can be distributed and installed on other systems.
