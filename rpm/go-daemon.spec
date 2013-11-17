%define debug_package %{nil}

Summary: Daemonize other programs
Name: go-daemon
Version: 1.1
Release: 1
License: BSD
Group: Utilities
URL: http://github.com/fiorix/go-daemon
Packager: Alexandre Fiori <fiorix@gmail.com>
Source: %{name}-%{version}.tar.gz
BuildRoot: %{_tmpdir}/%{name}-%{version}-%{release}
BuildRequires: gcc
BuildRequires: make

%description
Go daemon (or just "god") is a command line tool to
"daemonize" programs that originally only run in
foreground and write logs to the console.

%prep
%setup -q

%build
make

%install
make DESTDIR=%{buildroot} install

%files
%doc README.md
%{_bindir}/god

%changelog
* Wed Oct 24 2013 Alexandre Fiori <fiorix@gmail.com> 1.0-1
- Initial release

* Wed Oct 24 2013 Alexandre Fiori <fiorix@gmail.com> 1.1-1
- Improvements
- Bug fixes

* Sun Nov 17 2013 Alexandre Fiori <fiorix@gmail.com> 1.2
- Only rotate log file after opening new one on HUP
