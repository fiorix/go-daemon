# Copyright 2013-2026 Alexandre Fiori
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

all:
	mkdir -p $(HOME)/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	echo '%_topdir $(HOME)/rpmbuild' > ~/.rpmmacros
	make -C .. archive
	mv ../go-daemon-*.gz $(HOME)/rpmbuild/SOURCES
	cp go-daemon.spec $(HOME)/rpmbuild/SPECS
	rpmbuild -ba $(HOME)/rpmbuild/SPECS/go-daemon.spec

deps:
	yum install gcc make rpm-build redhat-rpm-config

.PHONY: all deps
