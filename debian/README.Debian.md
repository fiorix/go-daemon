# Go daemon

For detailed information about Go daemon please see the README.md file.

This file contains documentation about how to build the debian package from
the source code, available at http://github.com/fiorix/go-daemon.


## Building the debian package

Install dependencies:

	apt-get install build-essential debhelper dh-make fakeroot

Build the package:

	cd go-daemon
	dpkg-buildpackage -rfakeroot

Install it:

	cd ..
	dpkg -i go-daemon_1.0-1_<arch>.deb

The package can be distributed and installed on other systems.
