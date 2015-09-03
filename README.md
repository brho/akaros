About Akaros
============
Akaros is an open source, GPL-licensed operating system for manycore
architectures.  Its goal is to provide better support for parallel and
high-performance applications in the datacenter.  Unlike traditional OSs, which
limit access to certain resources (such as cores), Akaros provides native
support for application-directed resource management and 100% isolation from
other jobs running on the system.

Although not yet integrated as such, it is designed to operate as a low-level
node OS with a higher-level Cluster OS, such as [Mesos](http://mesos.apache.org/),
governing how resources are shared amongst applications running on each node.
Its system call API and "Many Core Process" abstraction better match the
requirements of a Cluster OS, eliminating many of the obstacles faced by other
systems when trying to isolate simultaneously running processes.  Moreover,
Akaros’s resource provisioning interfaces allow for node-local decisions to be
made that enforce the resource allocations set up by a Cluster OS.  This can be
used to simplify global allocation decisions, reduce network communication, and
ultimately promote more efficient sharing of resources.  There is limited
support for such functionality on existing operating systems.

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
Send an email to <akaros-request@lists.eecs.berkeley.edu>.

#### Want to report a bug?
Create a new issue [here](https://github.com/brho/akaros/issues).

#### Want to chat on IRC?
`brho` hangs out (usually alone) in #akaros on `irc.freenode.net`.
The other devs may pop in every now and then.

Contributing
============

Instructions on contributing can be found in
[Documentation/Contributing.md](Documentation/Contributing.md).
All contributed code is governed by the licenses detailed below.
