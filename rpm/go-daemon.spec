Summary: Daemonize other programs
Name: go-daemon
Version: 1.0
Release: 1
License: BSD
Group: Utilities
URL: http://github.com/fiorix/go-daemon
Packager: Alexandre Fiori <fiorix@gmail.com>
Source: %{name}-%{version}.tar.gz
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
