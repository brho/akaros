/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * lock_test: microbenchmark to measure different styles of spinlocks. */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <argp.h>

#include <tsc-compat.h>
#include <measure.h>

/* OS dependent #incs */
#include <parlib.h>
#include <vcore.h>
#include <timing.h>
#include <spinlock.h>
#include <mcs.h>
#include <arch/arch.h>
#include <event.h>

/* TODO: There's lot of work to do still, both on this program and on locking
 * and vcore code.  For some of the issues, I'll leave in the discussion /
 * answers, in case it comes up in the future (like when I read this in 8
 * months).
 *
 * BUGS / COMMENTARY
 * 		Occasional deadlocks when preempting and not giving back!
 * 			- with the new PDRs style, though that doesn't mean the older styles
 * 			don't have this problem
 * 			- shouldn't be any weaker than PDR.  they all check pred_vc to see
 * 			if they are running, and if not, they make sure someone runs
 * 			- could be weaker if we have an old value for the lockholder,
 * 			someone outside the chain, and we made sure they ran, and they do
 * 			nothing (spin in the 2LS or something?)
 * 				no, they should have gotten a msg about us being preempted,
 * 				since whoever we turn into gets the message about us swapping.
 * 			- anyway, it's not clear if this is with MCSPDR, event delivery,
 * 			preemption handling, or just an artifact of the test (less likely)
 * 		why aren't MCS locks in uth_ctx getting dealt with?
 * 			- because the event is handled, but the lock holder isn't run.  the
 * 			preemption was dealt with, but nothing saved the lock holder
 * 			- any uthread_ctx lockholder that gets preempted will get
 * 			interrupted, and other cores will handle the preemption.  but that
 * 			uthread won't run again without 2LS support.  either all spinners
 * 			need to be aware of the 'lockholder' (PDR-style), or the 2LS needs
 * 			to know when a uthread becomes a 'lockholder' to make sure it runs
 * 			via user-level preempts.  If the latter, this needs to happen
 * 			atomically with grabbing the lock, or else be able to handle lots of
 * 			fake 'lockholders' (like round-robin among all of them)
 * 		why is the delay more than the expected delay?
 * 			because it takes ~2ms to spawn and run a process
 * 			could do this in a separate process, instead of a script
 * 				could also consider not using pth_test and changing prov, but
 * 				driving it by yields and requests.  would also test the
 * 				alarm/wakeup code (process sets alarm, goes to sleep, wakes up
 * 				and requests X cores)
 * 		why do we get occasional preempt-storms? (lots of change_tos)
 * 			due to the MCS-PDR chain, which i tried fixing by adjusting the
 * 			number of workers down to the number of vcores
 * 			why isn't the worker adaptation working?
 * 				- it actually was working, and nr_workers == nr_vcores.  that
 * 				just wasn't the root cause.
 * 				- was expecting it to cut down on PDR kernel traffic
 * 			- still get periods of low perf
 * 				like O(100) preempt msgs per big preempt/prov
 * 				does it really take that much to work out an MCS-PDR?
 * 			- one thing is that if we fake vc ctx, we never receive preemption
 * 			events.  might be a bad idea.
 * 				- in general, yeah.  faking VC and turning off events can really
 * 				muck with things
 * 				- these events aren't necessarily delivered to a VC who will
 * 				check events any time soon (might be the last one in the chain)
 * 				- the core of the issue is that we have the right amount of
 * 				workers and vcores, but that the system isn't given a chance to
 * 				stabilize itself.  also, if we have some VCs that are just
 * 				sitting around, spinning in the 2LS, if those get preempted, no
 * 				one notices or cares (when faking vc_ctx / getting no events)
 * 			- there is a slight race where we might make someone run who isn't a
 * 			lockholder.  logically, its okay.  worst case, it would act like an
 * 			extra preempt and different startcore, which shouldn't be too bad.
 *
 * 		sanity check: does throughput match latency? (2.5GHz TSC, MCS lock)
 * 			ex: 5000 locks/ms = 5 locks/us = 200ns/lock = 500 ticks / lock
 * 			500 ticks * 31 workers (queue) = 15000 ticks
 * 			avg acquire time was around 14K.  seems fine..
 * 			when our MCSPDR throughput tanks (during preempts), it's around
 * 			400-500 locks/ms, which is around 2us/lock.  
 * 				when the locker on a preempted chain shows up, it needs to
 * 				change to the next one in line. 
 * 					- though that should be in parallel with the other
 * 					lockholders letting go.  shouldn't be that bad
 * 					- no, it is probably at the head of the chain very soon,
 * 					such that it is the bottleneck for the actual lock.  2us
 * 					seems possible
 *
 * 		what does it take to get out of a preemption with (old) MCS-PDR?
 * 			- these are now called pdro locks (old)
 * 			- for a single preempt, it will take 1..n-1 changes.  avg n/2
 * 			- for multiple preempts, it's nr_pre * that (avg np/2, worst np)
 * 			- for every unlock/reacquire cycle (someone unlocks, then rejoins
 * 			the list), its nr_preempts (aka, nr_workers - nr_vcores)
 * 			- if we need to have a specific worker get out of the chain, on
 * 			average, it'd take n/2 cycles (p*n/2 changes)  worst: np
 * 			- if we want to get multiple workers out, the worst case is still
 * 			np, but as p increases, we're more likely to approach n cycles
 * 			- so the current model is np for the initial hit (to move the
 * 			offline VCs to the end of the chain) and another np to get our
 * 			specific workers out of the chain and yielding (2np)
 *
 * 			- but even with 1 preempt, we're getting 80-200 changes per
 *
 * 			- it shouldn't matter that the sys_change_to is really slow, should
 * 			be the same amount of changes.  however, the preempted ones are
 * 			never really at the tail end of the chain - they should end up right
 * 			before the lockholder often.  while the sys_change_tos are slowly
 * 			moving towards the back of the chain, the locking code is quickly
 * 			removing (online) nodes from the head and putting them on the back.
 *
 * 			- end result: based on lock hold time and lock delay time, a
 * 			preempted VC stays in the MCS chain (swaps btw VC/nodes), and when
 * 			it is inside the chain, someone is polling to make them run.  with
 * 			someone polling, it is extremely unlikely that someone outside the
 * 			chain will win the race and be able to change_to before the in-chain
 * 			poller.  to clarify:
 * 				- hold time and delay time matter, since the longer they are,
 * 				the greater the amount of time the change_to percolation has to
 * 				get the preempted VCs to the end of the chain (where no one
 * 				polls them).
 * 				- at least one vcore is getting the event to handle the
 * 				preemption of the in-chain, offline VC.  we could change it so
 * 				every VC polls the preempt_evq, or just wait til whoever is
 * 				getting the messages eventually checks their messages (VC0)
 * 				- if there is an in-chain poller, they will notice the instant
 * 				the VC map changes, and then immediately change_to (and spin on
 * 				the proclock in the kernel).  there's almost no chance of a
 * 				normal preempt event handler doing that faster.  (would require
 * 				some IRQ latency or something serious).
 * 			- adding in any hold time trashes our microbenchmark's perf, but a
 * 			little delay time actually helps: (all with no preempts going on)
 * 				- mcspdr, no delay: 4200-4400 (-w31 -l10000, no faking, etc)
 * 				- mcspdr, d = 1: 4400-4800
 * 				- mcspdr, d = 2: 4200-5200
 * 				- as you add delay, it cuts down on contention for the
 * 				lock->lock cacheline.  but if you add in too much, you'll tank
 * 				throughput (since there is no contention at all).
 * 				- as we increase the delay, we cut down on the chance of the
 * 				preempt storm / preempt-stuck-in-the-chain, though it can still
 * 				happen, even with a delay of 10us
 * 			- maybe add in the lockholder again? (removed in 73701d6bfb)
 * 				- massively cuts performance, like 2x throughput, without
 * 				preempts
 * 				- it's ability to help depends on impl:
 * 					in one version (old style), it didn't help much at all
 * 					- in another (optimized lockholder setting), i can't even
 * 					see the throughput hit, it recovered right away, with O(5)
 * 					messages
 * 					- the diff was having the lockholder assign the vcoreid
 * 					before passing off to the next in the chain, so that there
 * 					is less time with having "no lockholder".  (there's a brief
 * 					period where the lockholder says it is the next person, who
 * 					still spins.  they'll have to make sure their pred runs)
 * 			-adj workers doesn't matter either...
 * 				- the 2LS and preemption handling might be doing this
 * 				automatically, when handle_preempt() does a
 * 				thread_paused() on its current_uthread.
 * 				- adj_workers isn't critical if we're using some locks
 * 				that check notif_pending.  eventually someone hears
 * 				about preempted VCs (assuming we can keep up)
 *
 * 			What about delays?  both hold and delay should make it easier to get
 * 			the preempted vcore to the end of the chain.  but do they have to be
 * 			too big to be reasonable?
 * 				- yes.  hold doesn't really help much until everything is
 * 				slower.  even with a hold of around 1.2us, we still have the
 * 				change_to-storms and lowered throughput.
 * 				- doing a combo helps too.  if you hold for 1ns (quite a bit
 * 				more actually, due to the overhead of ndelay, but sufficient to
 * 				be "doing work"), and delaying for around 7us before rejoining,
 * 				there's only about a 1/5 chance of a single preempt messing us
 * 				up
 * 					- though having multiple preempts outstanding make this less
 * 					likely to work.
 * 					- and it seems like if we get into the storm scenario, we
 * 					never really get out.  either we do quickly or never do.
 * 					depending on the workload, this could be a matter of luck
 *
 * 			So we could try tracking the lockholder, but only looking at it when
 * 			we know someone was preempted in the chain - specifically, when our
 * 			pred is offline.  when that happens, we don't change to them, we
 * 			make sure the lockholder is running.
 * 				- tracking takes us from 4200->2800 throughput or so for MCS
 * 				- 5200 -> 3700 or so for MCS in vc_ctx (__MCSPDR)
 * 				- main spike seems to be in the hold time.  bimodal distrib,
 * 				with most below 91 (the usual is everything packed below 70) and
 * 				a big spike around 320
 *
 * 	Summary:
 * 		So we need to have someone outside the chain change_to the one in the
 * 		chain o/w, someone will always be in the chain.  Right now, it's always
 * 		the next in line who is doing the changing, so a preempted vcore is
 * 		always still in the chain. 
 *
 * 		If the locking workload has some delaying, such as while holding the
 * 		lock or before reacquiring, the "change_to" storm might not be a
 * 		problem.  If it is, the only alternative I have so far is to check the
 * 		lockholder (which prevents a chain member from always ensuring their
 * 		pred runs).  This hurts the lock's scalability/performance when we
 * 		aren't being preempted.  On the otherhand, based on what you're doing
 * 		with the lock, one more cache miss might not be as big of a deal as in
 * 		lock_test.  Especially if when you get stormed, your throughput could be
 * 		terrible and never recover.
 *
 * 		Similar point: you can use spinpdr locks.  They have the PDR-benefits,
 * 		and won't induce the storm of change_tos.  However, this isn't much
 * 		better for contended locks.  They perform 2-3x worse (on c89) without
 * 		preemption.  Arguably, if you were worried about the preempt storms and
 * 		want scalability, you might want to use mcspdr with lockholders.
 *
 * 		The MCSPDRS (now just callced MCSPDR, these are default) locks can avoid
 * 		the storm, but at the cost of a little more in performance.  mcspdrs
 * 		style is about the same when not getting preempted from uth ctx compared
 * 		to mcspdr (slight drop).  When in vc ctx, it's about 10-20% perf hit
 * 		(PDRS gains little from --vc_ctx). 
 *
 * 		Turns out there is a perf hit to PDRS (and any non-stack based qnode)
 * 		when running on c89.  The issue is that after shuffling the vcores
 * 		around, they are no longer mapped nicely to pcores (VC0->PC1, VC1->PC2).
 * 		This is due to some 'false sharing' of the cachelines, caused mostly by
 * 		aggressive prefetching (notably the intel adjacent cacheline prefetcher,
 * 		which grabs two CLs at a time!).  Basically, stack-based qnodes are
 * 		qnodes that are very far apart in memory.  Cranking up the padding in
 * 		qnodes in the "qnodes-in-locks" style replicates this.
 *
 * 		For some info on the prefetching:
 * 			http://software.intel.com/en-us/articles/optimizing-application-performance-on-intel-coret-microarchitecture-using-hardware-implemented-prefetchers/
 * 			http://software.intel.com/en-us/forums/topic/341769
 *
 * 		Here's some rough numbers of the different styles for qnodes on c89.
 * 		'in order' is VCn->PC(n+1) (0->1, 1->2).  Worst order is with even VCs
 * 		on one socket, odds on the other.  the number of CLs is the size of a
 * 		qnode.  mcspdr is the new style (called mcspdrs in some places in this
 * 		document), with lock-based qnodes.  mcspdr2 is the same, but with
 * 		stack-based qnodes.  mcspdro is the old style (bad a recovery), stack
 * 		based, sometimes just called mcs-pdr
 *
 * 		with prefetchers disabled (MCS and DCU)
 * 			mcspdr   1CL  4.8-5.4 in order, 3.8-4.2 worst order
 * 			mcspdr   2CL          in order,         worst order
 * 			mcspdr   4CL  5.2-6.0 in order, 4.7-5.3 worst order
 * 			mcspdr   8CL  5.4-6.7 in order, 5.2-6.2 worst order
 * 			mcspdr  16CL  5.1-5.8 in order, 5.2-6.8 worst order
 * 			mcspdr2 stck          in order,         worst order
 * 			mcspdro stck  4-3.4.3 in order, 4.2-4.5 worst order
 * 			mcspdro-vcctx 4.8-7.0 in order, 5.3-6.7 worst order
 * 			can we see the 2 humps? 
 * 				mcspdr 1CL yes but less, varied, etc
 * 				mcspdr2 no
 *
 * 		test again with worst order with prefetchers enabled
 * 			mcspdr   1CL  3.8-4.0 in order, 2.6-2.7 worst order
 * 			mcspdr   2CL  4.2-4.4 in order, 3.8-3.9 worst order
 * 			mcspdr   4CL  4.5-5.2 in order, 4.0-4.2 worst order
 * 			mcspdr   8CL  4.4-5.1 in order, 4.3-4.7 worst order
 * 			mcspdr  16CL  4.4-4.8 in order, 4.4-5.3 worst order
 * 			mcspdr2 stck  3.0-3.0 in order, 2.9-3.0 worst order
 * 			mcspdro stck  4.2-4.3 in order, 4.2-4.4 worst order
 * 			mcspdro-vcctx 5.2-6.4 in order, 5.0-5.9 worst order
 * 			can we see the 2 humps?
 * 				mcspdrs 1CL yes, clearly
 * 				mcspdr2 no
 *
 * PROGRAM FEATURES
 * 		- verbosity?  vcoremap, preempts, the throughput and latency histograms?
 * 		- have a max workers option (0?) == max vcores
 * 		- would like to randomize (within bounds) the hold/delay times
 * 			- help avoid convoys with MCS locks
 *
 * PERFORMANCE:
 * 		pcore control?  (hyperthreading, core 0, cross socket?)
 * 			want some options for controlling which threads run where, or which
 * 			vcores are even used (like turning off hyperthreading)?
 * 		implement ticket spinlocks?  (more fair, more effects of preempts)
 * 			no simple way to do PDR either, other than 'check everyone'
 * 		MCS vs MCSPDR vs __MCSPDR
 * 			MCS seems slightly better than __MCSPDR (and it should)
 * 			MCSPDR is a bit worse than __MCSPDR
 * 				- the uth_disable/enable code seems to make a difference.
 * 				- i see why the latencies are worse, since they have extra work
 * 				to do, but the internal part that contends with other cores
 * 				shouldn't be affected, unless there's some other thing going on.
 * 				Or perhaps there isn't always someone waiting for the lock?
 * 				- faking VC ctx mostly negates the cost of MCSPDR vs __MCSPDR
 * 			things that made a big diff: CL aligning the qnodes, putting qnodes
 * 			on stacks, reading in the vcoreid once before ensuring()
 * 		both MCS CAS unlocks could use some branch prediction work
 * 		spinpdr locks are 2-3x faster than spinlocks...
 * 			test, test&set  vs the existing test&set, plus lots of asserts
 *
 * 		some delay (like 10us) lowers latency while maintaining throughput
 * 			- makes sense esp with MCS.  if you join the queue at the last
 * 			second, you'll measure lower latency than attempting right away
 * 			- also true for spinlocks
 * 			- we can probably figure out the max throughput (TP = f(delay)) for
 * 			each lock type
 *
 * 		hard to get steady numbers with MCS - different runs of the same test
 * 		will vary in throughput by around 15-30% (e.g., MCS varying from 3k-4k
 * 		L/ms)
 * 			- happens on c89 (NUMA) and hossin (UMA)
 * 			- spinlocks seem a little steadier.
 * 			- for MCS locks, the order in which they line up across the pcores
 * 			will matter.  like if on one run, i regularly hand off between cores
 * 			in the same socket and only do one cross-socket step
 * 			- run a lot of shorter ones to get a trend, for now
 * 			- might be correllated with spikes in held times (last bin)
 * 			- can't turn off legacy USB on c89 (SMM) - interferes with PXE
 *
 * PREEMPTS:
 * 		better preempt record tracking?
 * 			i just hacked some event-intercept and timestamp code together
 * 			maybe put it in the event library?
 * 			the timestamps definitely helped debugging
 *
 * 		is it true that if uthread code never spins outside a PDR lock, then it
 * 		doesn't need preemption IPIs?  (just someone checks the event at some
 * 		point). 
 * 			think so: so long as you make progress and when you aren't, you
 * 			check events (like if a uthread blocks on something and enters VC
 * 			ctx)
 * 		adjusting the number of workers, whether vcores or uthreads
 * 		- if you have more lockers than cores:
 * 			- spinpdr a worker will get starved (akaros) (without 2LS support)
 * 				- running this from uth context will cause a handle_events
 * 			- mcspdr will require the kernel to switch (akaros)
 * 			- spin (akaros) might DL (o/w nothing), (linux) poor perf
 * 			- mcs (akaros) will DL, (linux) poor perf
 * 			- poor perf (latency spikes) comes from running the wrong thread
 * 			sometimes
 * 			- deadlock comes from the lack of kernel-level context switching
 * 		- if we scale workers down to the number of active vcores:
 * 			- two things: the initial hit, and the steady state.  during the
 * 			initial hit, we can still deadlock, since we have more lockers than
 * 			cores
 * 				- non-pdr (akaros) could deadlock in the initial hit
 * 				- (akaros) steady state, everything is normal (just fewer cores)
 * 			- how can we adjust this in linux?
 * 				- if know how many cores you have, then futex wait the others
 * 				- need some way to wake them back up
 * 				- if you do this in userspace, you might need something PDR-like
 * 				to handle when the "2LS" code gets preempted
 * 			- as mentioned above, the problem in akaros is that the lock/unlock
 * 			might be happening too fast to get into the steady-state and recover
 * 			from the initial preemption
 * 		- one of our benefits is that we can adapt in userspace, with userspace
 * 		knowledge, under any circumstance.
 * 			- we have the deadlock windows (forcing PDR).
 * 			- in return, we can do this adaptation in userspace
 * 			- and (arguably) anyone who does this in userspace will need PDR
 *
 * MEASUREMENT (user/parlib/measure.c)
 * 		extract into its own library, for linux apps
 * 		print out raw TSC times?  might help sync up diff timelines
 * 		Need more latency bins, spinlocks vary too much
 * 		maybe we need better high/low too, since this hist looks bad too
 * 			or not center on the average?
 * 			for printing, its hard to know without already binning.
 * 			maybe bin once (latency?), then use that to adjust the hist?
 *
 * 		Had this on a spinlock:
 * 		[      32 -    35656] 1565231:
 * 		(less than 200 intermediate)
 * 	    [  286557 - 20404788]   65298: *
 *
 *		Samples per dot: 34782
 *		Total samples: 1640606
 *		Avg time   : 96658
 *		Stdev time : 604064.440882
 *		Coef Var   : 6.249503
 *			High coeff of var with serious outliers, adjusted bins
 *			50/75/90/99: 33079 / 33079 / 33079 / 290219 (-<860)
 *			Min / Max  : 32 / 20404788
 *		was 50/75/90 really within 860 of each other?
 *
 * 		when we are preempted and don't even attempt anything, say for 10ms, it
 * 		actually doesn't hurt our 50/75/90/99 too much.  we have a ridiculous
 * 		stddev and max, and high average, but there aren't any additional
 * 		attempts at locking to mess with the attempt-latency.  Only nr_vcores
 * 		requests are in flight during the preemption, but we can spit out around
 * 		5000 per ms when we aren't preempted.
 *
 */

const char *argp_program_version = "lock_test v0.1475263";
const char *argp_program_bug_address = "<akaros@lists.eecs.berkeley.edu>";

static char doc[] = "lock_test -- spinlock benchmarking";
static char args_doc[] = "-w NUM -l NUM -t LOCK";

#define OPT_VC_CTX 1
#define OPT_ADJ_WORKERS 2

static struct argp_option options[] = {
	{"workers",		'w', "NUM",	OPTION_NO_USAGE, "Number of threads/cores"},
	{0, 0, 0, 0, ""},
	{"loops",		'l', "NUM",	OPTION_NO_USAGE, "Number of loops per worker"},
	{0, 0, 0, 0, ""},
	{"type",		't', "LOCK",OPTION_NO_USAGE, "Type of lock to use.  "
	                                             "Options:\n"
	                                             "\tmcs\n"
	                                             "\tmcscas\n"
	                                             "\tmcspdr\n"
	                                             "\tmcspdro\n"
	                                             "\t__mcspdro\n"
	                                             "\tspin\n"
	                                             "\tspinpdr"},
	{0, 0, 0, 0, "Other options (not mandatory):"},
	{"adj_workers",	OPT_ADJ_WORKERS, 0,	0, "Adjust workers such that the "
	                                       "number of workers equals the "
	                                       "number of vcores"},
	{"vc_ctx",		OPT_VC_CTX, 0,	0, "Run threads in mock-vcore context"},
	{0, 0, 0, 0, ""},
	{"hold",		'h', "NSEC",	0, "nsec to hold the lock"},
	{"delay",		'd', "NSEC",	0, "nsec to delay between grabs"},
	{"print",		'p', "ROWS",	0, "Print ROWS of optional measurements"},
	{ 0 }
};

struct prog_args {
	int							nr_threads;
	int							nr_loops;
	int							hold_time;
	int							delay_time;
	int							nr_print_rows;
	bool						fake_vc_ctx;
	bool						adj_workers;
	void *(*lock_type)(void *arg);
};
struct prog_args pargs = {0};

/* Globals */
struct time_stamp {
	uint64_t pre;
	uint64_t acq;
	uint64_t un;
	bool valid;
};
struct time_stamp **times;
bool run_locktest = TRUE;
pthread_barrier_t start_test;

/* Locking functions.  Define globals here, init them in main (if possible), and
 * use the lock_func() macro to make your thread func. */

spinlock_t spin_lock = SPINLOCK_INITIALIZER;
struct spin_pdr_lock spdr_lock = SPINPDR_INITIALIZER;
struct mcs_lock mcs_lock = MCS_LOCK_INIT;
struct mcs_pdr_lock mcspdr_lock;
struct mcs_pdro_lock mcspdro_lock = MCSPDRO_LOCK_INIT;

#define lock_func(lock_name, lock_cmd, unlock_cmd)                             \
void *lock_name##_thread(void *arg)                                            \
{                                                                              \
	long thread_id = (long)arg;                                                \
	int hold_time = ACCESS_ONCE(pargs.hold_time);                              \
	int delay_time = ACCESS_ONCE(pargs.delay_time);                            \
	int nr_loops = ACCESS_ONCE(pargs.nr_loops);                                \
	bool fake_vc_ctx = ACCESS_ONCE(pargs.fake_vc_ctx);                         \
	bool adj_workers = ACCESS_ONCE(pargs.adj_workers);                         \
	uint64_t pre_lock, acq_lock, un_lock;                                      \
	struct time_stamp *this_time;                                              \
	struct mcs_lock_qnode mcs_qnode = MCS_QNODE_INIT;                          \
	struct mcs_pdro_qnode pdro_qnode = MCSPDRO_QNODE_INIT;                     \
	/* guessing a unique vcoreid for vcoreid for the __mcspdr test.  if the
	 * program gets preempted for that test, things may go nuts */             \
	pdro_qnode.vcoreid = thread_id - 1;                                        \
	/* Wait til all threads are created.  Ideally, I'd like to busywait unless
	 * absolutely critical to yield */                                         \
	pthread_barrier_wait(&start_test);                                         \
	if (fake_vc_ctx) {                                                         \
		/* tells the kernel / other vcores we're in vc ctx */                  \
		uth_disable_notifs();                                                  \
		/* tricks ourselves into believing we're in vc ctx */                  \
		__vcore_context = TRUE;                                                \
	}                                                                          \
	for (int i = 0; i < nr_loops; i++) {                                       \
		if (!run_locktest)                                                     \
			break;                                                             \
		pre_lock = read_tsc_serialized();                                      \
                                                                               \
		lock_cmd                                                               \
                                                                               \
		acq_lock = read_tsc_serialized();                                      \
		if (hold_time)                                                         \
			ndelay(hold_time);                                                 \
                                                                               \
		unlock_cmd                                                             \
                                                                               \
		un_lock = read_tsc_serialized();                                       \
		this_time = &times[thread_id][i];                                      \
		this_time->pre = pre_lock;                                             \
		this_time->acq = acq_lock;                                             \
		this_time->un = un_lock;                                               \
		/* Can turn these on or off to control which samples we gather */      \
		this_time->valid = TRUE;                                               \
		/* this_time->valid = (num_vcores() == max_vcores());  */              \
                                                                               \
		if (delay_time)                                                        \
			ndelay(delay_time);                                                \
		/* worker thread ids are 0..n-1.  if we're one of the threads that's
		 * beyond the VC count, we yield. */                                   \
		if (adj_workers && num_vcores() < thread_id + 1) {                     \
			if (fake_vc_ctx) {                                                 \
				__vcore_context = FALSE;                                       \
				uth_enable_notifs();                                           \
			}                                                                  \
			/* we'll come back up once we have enough VCs running */           \
			pthread_yield();                                                   \
			if (fake_vc_ctx) {                                                 \
				uth_disable_notifs();                                          \
				__vcore_context = TRUE;                                        \
			}                                                                  \
		}                                                                      \
		cmb();                                                                 \
	}                                                                          \
	/* First thread to finish stops the test */                                \
	run_locktest = FALSE;                                                      \
	if (fake_vc_ctx) {                                                         \
		__vcore_context = FALSE;                                               \
		uth_enable_notifs();                                                   \
	}                                                                          \
	return arg;                                                                \
}

/* Defines locking funcs like "mcs_thread" */
lock_func(mcs,
          mcs_lock_lock(&mcs_lock, &mcs_qnode);,
          mcs_lock_unlock(&mcs_lock, &mcs_qnode);)
lock_func(mcscas,
          mcs_lock_lock(&mcs_lock, &mcs_qnode);,
          mcs_lock_unlock_cas(&mcs_lock, &mcs_qnode);)
lock_func(mcspdr,
          mcs_pdr_lock(&mcspdr_lock);,
          mcs_pdr_unlock(&mcspdr_lock);)
lock_func(mcspdro,
          mcs_pdro_lock(&mcspdro_lock, &pdro_qnode);,
          mcs_pdro_unlock(&mcspdro_lock, &pdro_qnode);)
lock_func(__mcspdro,
          __mcs_pdro_lock(&mcspdro_lock, &pdro_qnode);,
          __mcs_pdro_unlock(&mcspdro_lock, &pdro_qnode);)
lock_func(spin,
          spinlock_lock(&spin_lock);,
          spinlock_unlock(&spin_lock);)
lock_func(spinpdr,
          spin_pdr_lock(&spdr_lock);,
          spin_pdr_unlock(&spdr_lock);)

static int get_acq_latency(void **data, int i, int j, uint64_t *sample)
{
	struct time_stamp **times = (struct time_stamp**)data;
	/* 0 for initial time means we didn't measure */
	if (times[i][j].pre == 0)
		return -1;
	/* can optionally throw out invalid times (keep this in sync with the
	 * lock_test macro, based on what you want to meaasure. */
	#if 0
	if (!times[i][j].valid)
		return -1;
	#endif
	*sample = times[i][j].acq - times[i][j].pre - get_tsc_overhead();
	return 0;
}

static int get_hld_latency(void **data, int i, int j, uint64_t *sample)
{
	struct time_stamp **times = (struct time_stamp**)data;
	/* 0 for initial time means we didn't measure */
	if (times[i][j].pre == 0)
		return -1;
	*sample = times[i][j].un - times[i][j].acq - get_tsc_overhead();
	return 0;
}

static int get_acq_timestamp(void **data, int i, int j, uint64_t *sample)
{
	struct time_stamp **times = (struct time_stamp**)data;
	/* 0 for initial time means we didn't measure */
	if (times[i][j].pre == 0)
		return -1;
	*sample = times[i][j].acq;
	return 0;
}

/* Lousy event intercept.  build something similar in the event library? */
#define MAX_NR_EVENT_TRACES 1000
uint64_t preempts[MAX_NR_EVENT_TRACES] = {0};
uint64_t indirs[MAX_NR_EVENT_TRACES] = {0};
atomic_t preempt_idx;
atomic_t indir_idx;
atomic_t preempt_cnt;
atomic_t indir_cnt;

static void handle_preempt(struct event_msg *ev_msg, unsigned int ev_type)
{
	unsigned long my_slot = atomic_fetch_and_add(&preempt_idx, 1);
	if (my_slot < MAX_NR_EVENT_TRACES)
		preempts[my_slot] = read_tsc();
	atomic_inc(&preempt_cnt);
	handle_vc_preempt(ev_msg, ev_type);
}

static void handle_indir(struct event_msg *ev_msg, unsigned int ev_type)
{
	unsigned long my_slot = atomic_fetch_and_add(&indir_idx, 1);
	if (my_slot < MAX_NR_EVENT_TRACES)
		indirs[my_slot] = read_tsc();
	atomic_inc(&indir_cnt);
	handle_vc_indir(ev_msg, ev_type);
}

/* Helper, prints out the preempt trace */
static void print_preempt_trace(uint64_t starttsc, int nr_print_rows)
{
	/* reusing nr_print_rows for the nr preempt/indirs rows as well */
	int preempt_rows = MIN(MAX_NR_EVENT_TRACES, nr_print_rows);
	if (pargs.fake_vc_ctx) {
		printf("No preempt trace available when faking vc ctx\n");
		return;
	}
	printf("\n");
	printf("Nr Preempts: %d\n", atomic_read(&preempt_cnt));
	printf("Nr Indirs  : %d\n", atomic_read(&indir_cnt));
	if (preempt_rows)
		printf("Preempt/Indir events:\n-----------------\n");
	for (int i = 0; i < preempt_rows; i++) {
		if (preempts[i])
			printf("Preempt %3d at %6llu\n", i, tsc2msec(preempts[i]
			                                             - starttsc));
	}
	for (int i = 0; i < preempt_rows; i++) {
		if (indirs[i])
			printf("Indir   %3d at %6llu\n", i, tsc2msec(indirs[i]
			                                             - starttsc));
	}
}

/* Make sure we have enough VCs for nr_threads, pref 1:1 at the start */
static void os_prep_work(int nr_threads)
{
	if (nr_threads > max_vcores()) {
		printf("Too many threads (%d) requested, can't get more than %d vc\n",
		       nr_threads, max_vcores());
		exit(-1);
	}
	atomic_init(&preempt_idx, 0);
	atomic_init(&indir_idx, 0);
	atomic_init(&preempt_cnt, 0);
	atomic_init(&indir_cnt, 0);
	pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
	pthread_need_tls(FALSE);
	pthread_lib_init();					/* gives us one vcore */
	ev_handlers[EV_VCORE_PREEMPT] = handle_preempt;
	ev_handlers[EV_CHECK_MSGS] = handle_indir;
	if (pargs.fake_vc_ctx) {
		/* need to disable events when faking vc ctx.  since we're looping and
		 * not handling events, we could run OOM */
		clear_kevent_q(EV_VCORE_PREEMPT);
		clear_kevent_q(EV_CHECK_MSGS);
	}
	if (vcore_request(nr_threads - 1)) {
		printf("Failed to request %d more vcores, currently have %d\n",
		       nr_threads - 1, num_vcores());
		exit(-1);
	}
	for (int i = 0; i < nr_threads; i++) {
		printd("Vcore %d mapped to pcore %d\n", i,
		       __procinfo.vcoremap[i].pcoreid);
	}
}

/* Argument parsing */
static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
	struct prog_args *pargs = state->input;
	switch (key) {
		case 'w':
			pargs->nr_threads = atoi(arg);
			if (pargs->nr_threads < 0) {
				printf("Negative nr_threads...\n\n");
				argp_usage(state);
			}
			break;
		case 'l':
			pargs->nr_loops = atoi(arg);
			if (pargs->nr_loops < 0) {
				printf("Negative nr_loops...\n\n");
				argp_usage(state);
			}
			break;
		case OPT_ADJ_WORKERS:
			pargs->adj_workers = TRUE;
			break;
		case OPT_VC_CTX:
			pargs->fake_vc_ctx = TRUE;
			break;
		case 'h':
			pargs->hold_time = atoi(arg);
			if (pargs->hold_time < 0) {
				printf("Negative hold_time...\n\n");
				argp_usage(state);
			}
			break;
		case 'd':
			pargs->delay_time = atoi(arg);
			if (pargs->delay_time < 0) {
				printf("Negative delay_time...\n\n");
				argp_usage(state);
			}
			break;
		case 'p':
			pargs->nr_print_rows = atoi(arg);
			if (pargs->nr_print_rows < 0) {
				printf("Negative print_rows...\n\n");
				argp_usage(state);
			}
			break;
		case 't':
			if (!strcmp("mcs", arg)) {
				pargs->lock_type = mcs_thread;
				break;
			}
			if (!strcmp("mcscas", arg)) {
				pargs->lock_type = mcscas_thread;
				break;
			}
			if (!strcmp("mcspdr", arg)) {
				pargs->lock_type = mcspdr_thread;
				break;
			}
			if (!strcmp("mcspdro", arg)) {
				pargs->lock_type = mcspdro_thread;
				break;
			}
			if (!strcmp("__mcspdro", arg)) {
				pargs->lock_type = __mcspdro_thread;
				break;
			}
			if (!strcmp("spin", arg)) {
				pargs->lock_type = spin_thread;
				break;
			}
			if (!strcmp("spinpdr", arg)) {
				pargs->lock_type = spinpdr_thread;
				break;
			}
			printf("Unknown locktype %s\n\n", arg);
			argp_usage(state);
			break;
		case ARGP_KEY_ARG:
			printf("Warning, extra argument %s ignored\n\n", arg);
			break;
		case ARGP_KEY_END:
			if (!pargs->nr_threads) {
				printf("Must select a number of threads.\n\n");
				argp_usage(state);
				break;
			}
			if (!pargs->nr_loops) {
				printf("Must select a number of loops.\n\n");
				argp_usage(state);
				break;
			}
			if (!pargs->lock_type) {
				printf("Must select a type of lock.\n\n");
				argp_usage(state);
				break;
			}
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

int main(int argc, char** argv)
{
	pthread_t *worker_threads;
	void *dummy_retval;
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	long usec_diff;
	uint64_t starttsc;
	int nr_threads, nr_loops;
	struct sample_stats acq_stats, hld_stats;

	argp_parse(&argp, argc, argv, 0, 0, &pargs);
	nr_threads = pargs.nr_threads;
	nr_loops = pargs.nr_loops;
	mcs_pdr_init(&mcspdr_lock);

	worker_threads = malloc(sizeof(pthread_t) * nr_threads);
	if (!worker_threads) {
		perror("pthread_t malloc failed:");
		exit(-1);
	}
	printf("Making %d workers of %d loops each, %sadapting workers to vcores, "
	       "and %sfaking vcore context\n", nr_threads, nr_loops,
	       pargs.adj_workers ? "" : "not ",
	       pargs.fake_vc_ctx ? "" : "not ");
	pthread_barrier_init(&start_test, NULL, nr_threads);

	times = malloc(sizeof(struct time_stamp *) * nr_threads);
	assert(times);
	for (int i = 0; i < nr_threads; i++) {
		times[i] = malloc(sizeof(struct time_stamp) * nr_loops);
		if (!times[i]) {
			perror("Record keeping malloc");
			exit(-1);
		}
		memset(times[i], 0, sizeof(struct time_stamp) * nr_loops);
	}
	printf("Record tracking takes %d bytes of memory\n",
	       nr_threads * nr_loops * sizeof(struct time_stamp));
	os_prep_work(nr_threads);	/* ensure we have enough VCs */
	/* Doing this in MCP ctx, so we might have been getting a few preempts
	 * already.  Want to read start before the threads pass their barrier */
	starttsc = read_tsc();
	/* create and join on yield */
	for (long i = 0; i < nr_threads; i++) {
		if (pthread_create(&worker_threads[i], NULL, pargs.lock_type,
		                   (void*)i))
			perror("pth_create failed");
	}
	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");
	for (int i = 0; i < nr_threads; i++) {
		pthread_join(worker_threads[i], &dummy_retval);
	}
	if (gettimeofday(&end_tv, 0))
		perror("End time error...");

	printf("Acquire times (TSC Ticks)\n---------------------------\n");
	acq_stats.get_sample = get_acq_latency;
	compute_stats((void**)times, nr_threads, nr_loops, &acq_stats);

	printf("Held times (from acq til rel done) (TSC Ticks)\n------\n");
	hld_stats.get_sample = get_hld_latency;
	compute_stats((void**)times, nr_threads, nr_loops, &hld_stats);

	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);
	printf("Time to run: %d usec\n", usec_diff);

	printf("\nLock throughput:\n-----------------\n");
	/* throughput for the entire duration (in ms), 1ms steps.  print as many
	 * steps as they ask for (up to the end of the run). */
	print_throughput((void**)times, usec_diff / 1000 + 1, msec2tsc(1),
	                 pargs.nr_print_rows,
	                 starttsc, nr_threads,
	                 nr_loops, get_acq_timestamp);
	print_preempt_trace(starttsc, pargs.nr_print_rows);
	printf("Done, exiting\n");
}
