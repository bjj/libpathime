# Security

Report vulnerabilities privately through GitHub's security advisories:
<https://github.com/bjj/libpathime/security/advisories/new>. Reports get a
response within a week. Please do not open a public issue for anything you
believe is exploitable.

Only the latest release is supported; pre-1.0 there are no maintenance
branches, and a fix ships as the next release.

Scope worth knowing when assessing impact: libpathime is a synchronous
library that processes key events and reads its own dictionary data from
`pathime-data/`. It runs no server, opens no sockets, and in the intended
arrangement does not consume untrusted input — the table sources it compiles
at build time and the dictionaries it reads at run time ship with it. A
consumer that feeds it attacker-controlled table files or dictionary data is
outside that arrangement and should say so in a report.

The same process applies to the binding repositories
([libpathime-sharp](https://github.com/bjj/libpathime-sharp),
[libpathime-python](https://github.com/bjj/libpathime-python)); use the
advisory page of the repository the problem lives in, or this one when in
doubt.
