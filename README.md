About Akaros
============
Akaros is an open source, GPL-licensed operating system for manycore
architectures.  Its goal is to provide better support for parallel and
high-performance applications in the datacenter.  Unlike traditional OSs, which
limit access to certain resources (such as cores), Akaros provides native
support for application-directed resource management and 100% isolation from
other jobs running on the system.

Although not yet integrated as such, it is designed to operate as a low-level
node OS with a higher-level Cluster OS, such as
[Mesos](http://mesos.apache.org/), governing how resources are shared amongst
applications running on each node.  Its system call API and "Many Core Process"
abstraction better match the requirements of a Cluster OS, eliminating many of
the obstacles faced by other systems when trying to isolate simultaneously
running processes.  Moreover, Akaros’s resource provisioning interfaces allow
for node-local decisions to be made that enforce the resource allocations set
up by a Cluster OS.  This can be used to simplify global allocation decisions,
reduce network communication, and ultimately promote more efficient sharing of
resources.  There is limited support for such functionality on existing
operating systems.

Akaros is still very young, but preliminary results show that processes running
on Akaros have an order of magnitude less noise than on Linux, as well as fewer
periodic signals, resulting in better CPU isolation.  Additionally, its
non-traditional threading model has been shown to outperform the Linux NPTL
across a number of representative application workloads.  This includes a 3.4x
faster thread context switch time, competitive performance for the NAS parallel
benchmark suite, and a 6% increase in throughput over nginx for a simple
thread-based webserver we wrote.  We are actively working on expanding Akaros's
capabilities even further.

Visit us at [akaros.org](http://www.akaros.org)

Installation
============

Instructions on installation and getting started with Akaros can be found in
[GETTING_STARTED.md](GETTING_STARTED.md)

Documentation
=============

Our current documentation is very lacking, but it is slowly getting better over
time.  Most documentation is typically available in the [Documentation/](Documentation/)
directory.  However, many of these documents are outdated, and some general
cleanup is definitely in order.

Mailing Lists
=============

#### Want to join the developers mailing list?
Send an email to [akaros+subscribe@googlegroups.com](mailto:akaros%2Bsubscribe@googlegroups.com).

Or visit our [google group](https://groups.google.com/forum/#!forum/akaros)
and click "Join Group"

#### Want to report a bug?
Create a new issue [here](https://github.com/brho/akaros/issues).

#### Want to chat on IRC?
`brho` hangs out (usually alone) in #akaros on `irc.freenode.net`.
The other devs may pop in every now and then.

Contributing
============

Instructions on contributing can be found in
[Documentation/Contributing.md](Documentation/Contributing.md).

License
============
The Akaros repository contains a mix of code from different projects across a
few top-level directories.  The kernel is in `kern/`, userspace libraries are
in `user/`, and a variety of tools can be found in `tools/`, including the
toolchain.

The Akaros kernel is licensed under the [GNU General Public License, version
2](http://www.gnu.org/licenses/gpl-2.0.txt).  Our kernel is made up of code
from a number of other systems.  Anything written for the Akaros kernel is
licensed "GPLv2 or later".  However, other code, such as from Linux and Plan 9,
are licensed GPLv2, without the "or later" clause.  There is also code from
BSD, Xen, JOS, and Plan 9 derivatives.  As a whole, the kernel is licensed
GPLv2.

Note that the Plan 9 code that is a part of Akaros is also licensed under the
Lucent Public License.  The University of California, Berkeley, has been
authorised by Alcatel-Lucent to release all Plan 9 software previously governed
by the Lucent Public License, Version 1.02 under the GNU General Public
License, Version 2.  Akaros derives its Plan 9 code from this UCB release.  For
more information, see [LICENSE-plan9](LICENSE-plan9) or
[here](http://akaros.cs.berkeley.edu/files/Plan9License).

Our user code is likewise from a mix of sources.  All code written for Akaros,
such as `user/parlib/`, is licensed under the [GNU
LGPLv2.1](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt), or later.
Plan 9 libraries, including `user/iplib` and `user/ndblib` are licensed under
the LGPLv2.1, but without the "or later".  See each library for details.

Likewise, `tools/` is a collection of various code.  All of our contributions
to existing code bases, such as GCC, glibc, and busybox, are licensed under
their respective projects' licenses.
