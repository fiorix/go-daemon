# Copyright 2013-2014 Alexandre Fiori
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

all:
	mkdir -p $(HOME)/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	echo '%_topdir $(HOME)/rpmbuild' > ~/.rpmmacros
	make -C .. archive
	mv ../go-daemon-*.gz $(HOME)/rpmbuild/SOURCES
	cp go-daemon.spec $(HOME)/rpmbuild/SPECS
	cd $(HOME)/rpmbuild/SPECS
	rpmbuild -ba go-daemon.spec

deps:
	yum install gcc make rpm-build redhat-rpm-config
