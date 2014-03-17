/* Barret Rhoden
 *
 * Code heavily ported from "How to Benchmark Code Execution Times on Intel(R)
 * IA-32 and IA-64 Instruction Set Architectures" for linux, except for
 * check_timing_stability().
 *
 * The idea behind this was that the traditional style of using rdtsc was to
 * call:
 * 			cpuid;
 * 			rdtsc;
 * since rdtsc does no serialization (meaning later instructions can get
 * executed before it, or vice versa).  While this first cpuid isn't a big deal,
 * doing this in pairs means reading the end time also measures cpuid.  This is
 * a problem since cpuid can vary quite a bit.
 *
 * If we use rdtscp for the end call, we can put the cpuid after rdtscp, thereby
 * not including cpuid's overhead (and variability) in our measurement.  That's
 * where the intel doc ends.  For more info, check out:
 * 		http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/ia-32-ia-64-benchmark-code-execution-paper.pdf
 *
 * Note that the Intel SDM says you can serialize rdtsc with lfence, such as:
 * 			lfence;
 * 			rdtsc;
 * Linux uses this (mfence on amd64, lfence on intel).  For more info:
 * 		https://lkml.org/lkml/2008/1/2/353
 * Note this use of lfence before rdtsc is supposedly serializing any
 * instruction, not just loads.  Some stranger on the internet suggested that
 * while lfence only serializes memory (and not arbitrary instructions), in
 * actual hardware there is no point to reorder non-memory instructions around
 * rdtsc:
 * 		http://stackoverflow.com/questions/12631856/difference-between-rdtscp-rdtsc-memory-and-cpuid-rdtsc
 * 		(look for janneb's response to questions about his comment)
 *
 * Its not clear from what anyone writes as to whether or not you need to
 * serialize below rdtsc.  Supposedly, you'd need cpuid/lfence on both sides of
 * rdtsc to prevent reordering in both directions.  Andi Kleen does this in a
 * few places
 * 		https://lkml.org/lkml/2008/1/7/276
 * though other places in the kernel suggest it is unnecessary (at least for
 * loads:
 * 		http://lxr.linux.no/#linux+v3.8.2/arch/x86/kvm/x86.c#L1258
 * The intel docs don't mention it (otherwise we would be told to use
 * lfence;rdtsc;lfence).  The howto this file is based off of didn't mention it
 * either, other than to say rdtscp needs to serialize from below.  AFAIK,
 * rdtscp is like rdtsc, except that it serializes from above (and also returns
 * the CPU id).  If rdtscp needs to serialize from below, then so should rdtsc.
 *
 * That being said, if these rdtsc(p) calls do not need serialization from
 * below, then rdtscp (which provides serialization from above) should not need
 * any additional serialization (lfence or cpuid).
 *
 * I tried out a few options for the assembly for the start and end time
 * measurements, using the intel benchmark.  The benchmark reports variance, max
 * deviation, and minimum per inner loop (line), as well as an overall variance,
 * max dev, and variance of vars/mins.
 *
 * CASE    START ASM            END ASM
 * ---------------------------------------------------
 * case 0: cpuid;rdtsc;			cpuid;rdtscp;
 * case 1: cpuid;rdtsc;			rdtscp;cpuid; (or rdtscp;lfence)
 * case 2: lfence;rdtsc;		rdtscp;cpuid; (or rdtscp;lfence)
 * case 3: rdtscp;				rdtscp;cpuid; (or rdtscp;lfence)
 * case 4: rdtscp;				rdtscp;
 * case 5: lfence;rdtsc;		lfence;rdtsc;
 * case 6: lfence;rdtsc;lfence;	lfence;rdtsc;lfence;
 *
 * Note I only ran these a couple times, with 1000x10000, and I did notice some
 * slight variation between runs (on cases 3 and 4).
 *
 * case 0:       wildly variant, variance of variances wasn't 0, etc (as
 * reported by intel).
 * case 0:  some lines     0 var, 0-8 max dev, 420 min
 * case 0: other lines 50-60 var,  20 max dev, 388 min
 *
 * For all of the other cases, variance of variances and of minvalues was 0.
 *
 * case 1: most lines 2-3 var, 4 max dev, 44 min, 2 var 4 max dev overall
 * case 2: most lines 2-3 var, 4 max dev, 44 min, 2 var 4 max dev overall
 * case 3: most lines   0 var, 0 max dev, 32 min, 0 var 0 max dev overall
 * case 4: most lines   0 var, 0 max dev, 32 min, 0 var 4 max dev overall
 * case 5: most lines   3 var, 4 max dev, 28 min, 2 var 4 max dev overall
 * case 6: most lines   3 var, 4 max dev, 44 min, 2 var 4 max dev overall
 *
 * 		case 1-3: cpuid vs lfence: both seem to work the same and have no effect
 * 		(since they are outside the loop)
 *
 * So running with rdtscp for both start and stop (case 3 and 4) had the least
 * amount of variance (both per line and total).  When these cases have had any
 * deviation, it was because one run had a min of 28, but o/w was 32.  (1 out of
 * 10000000, often the first run).
 *
 * All the others have a little deviation, but with a more stable min.  Again,
 * this is taken mostly from a small number of runs (of 1kx10k).
 *
 * Note that cases 5 and 6 have lfences inside the measurement area, and this
 * does not seem to cause problems the same way cpuid does.  However, lfences
 * inside the critical section (esp after whatever code we are measuring)
 * probably will have an effect on real code that has made memory accesses (keep
 * in mind we need to do an mfence on amd64 here).
 *
 * All that being said, it's not clear which option to use.  Ideally, we want
 * an isolated region of code to be measured, with very little variance and max
 * deviation.  If cases 1-6 are all the same in terms of protection (which I'm
 * not sure about), then 3-4 look nice.  However, the fact that sometimes the
 * min is less than 'normal', means that we could get negative numbers for some
 * measurements (the goal is to determine the overhead and subtract that from
 * our total measurement, and if we think the overhead is 32 but was actually 28
 * for a run, we could have issues). 
 *
 * But wait, there's more:
 * 
 * When we add code around (and inside) the measurement, things get even worse:
 * - If we put variable (a volatile btw) = j + i; in the loop, there's no real
 *   change.  I checked cases 1, 4 and 5, 1 being the intel recommended, 4 being
 *   the one with the best variance with no code, and 5 being a good symmetric
 *   choice (same on start and end).  Case 1 had no change at all.  4 and 5 had
 *   little change (min was the same, occasional deviation).  Note that case 5
 *   doesn't use rdtscp at the end either.
 * - If we put in variable = i; as well, the minimum still is unaffected, and
 *   there is a little more variance.  For example, for case 4, the min is still
 *   32, and sometimes you get a 36.
 *
 * If we add more code (like a for loop that grows in length with each outer
 * loop), eventually we can detect the existence of the instructions.  The Intel
 * author talks about this in 3.3 when he finds the resolution of the benchmark.
 *
 * My hunch is that the rdtsc(p) calls hide the latency of some previous
 * instructions, regardless of serialization commands.  We see this 'hiding' of
 * the cost of instructions regardless of whether or not the first or last
 * commands are rdtscp (I'm more concerned with the end time call, which is
 * where this hiding may be happening).  Perhaps the pipeline needs to be
 * drained (or something), and it takes a certain amount of time to do so, 
 * regardless of a few extra instructions squeezed in.  Meaning we can't tell
 * the difference between 0 and a few cycles, and probably a few cycles are
 * 'free' / hidden by the rdtsc call. 
 *
 * Bottom line?  Our measurements are inexact, despite the stable minimum and
 * low variance.  Everything will be +/- our max deviation, as well as
 * potentially underestimating by a few cycles/ticks.  One thing we can do is
 * try to see what the resolution is of the different methods.
 *
 * case 1: cpuid;rdtsc;			rdtscp;cpuid; (or rdtscp;lfence)
 * -------------------
 * loop_size:0 >>>> variance(cycles): 3; max_deviation: 8; min time: 44
 * loop_size:1 >>>> variance(cycles): 6; max_deviation: 28; min time: 44
 * loop_size:2 >>>> variance(cycles): 4; max_deviation: 16; min time: 44
 * loop_size:3 >>>> variance(cycles): 12; max_deviation: 44; min time: 44
 * loop_size:4 >>>> variance(cycles): 10; max_deviation: 32; min time: 44
 * loop_size:5 >>>> variance(cycles): 10; max_deviation: 32; min time: 44
 * loop_size:6 >>>> variance(cycles): 12; max_deviation: 36; min time: 44
 * loop_size:7 >>>> variance(cycles): 5; max_deviation: 32; min time: 48
 * loop_size:8 >>>> variance(cycles): 16; max_deviation: 52; min time: 48
 * loop_size:9 >>>> variance(cycles): 13; max_deviation: 48; min time: 52
 * loop_size:10 >>>> variance(cycles): 9; max_deviation: 36; min time: 52
 * loop_size:11 >>>> variance(cycles): 16; max_deviation: 64; min time: 56
 *
 * case 4: rdtscp;				rdtscp;
 * -------------------
 * loop_size:0 >>>> variance(cycles): 1; max_deviation: 20; min time: 32
 * loop_size:1 >>>> variance(cycles): 12; max_deviation: 36; min time: 36
 * loop_size:2 >>>> variance(cycles): 13; max_deviation: 32; min time: 36
 * loop_size:3 >>>> variance(cycles): 7; max_deviation: 32; min time: 40
 * loop_size:4 >>>> variance(cycles): 1; max_deviation: 16; min time: 44
 * loop_size:5 >>>> variance(cycles): 4; max_deviation: 28; min time: 44
 * loop_size:6 >>>> variance(cycles): 12; max_deviation: 48; min time: 44
 * loop_size:7 >>>> variance(cycles): 8; max_deviation: 32; min time: 44
 * loop_size:8 >>>> variance(cycles): 10; max_deviation: 48; min time: 48
 *
 * case 5: lfence;rdtsc;		lfence;rdtsc;
 * -------------------
 * loop_size:0 >>>> variance(cycles): 3; max_deviation: 12; min time: 28
 * loop_size:1 >>>> variance(cycles): 8; max_deviation: 28; min time: 32
 * loop_size:2 >>>> variance(cycles): 8; max_deviation: 28; min time: 32
 * loop_size:3 >>>> variance(cycles): 6; max_deviation: 28; min time: 32
 * loop_size:4 >>>> variance(cycles): 2; max_deviation: 24; min time: 36
 * loop_size:5 >>>> variance(cycles): 6; max_deviation: 28; min time: 36
 * loop_size:6 >>>> variance(cycles): 11; max_deviation: 44; min time: 36
 * loop_size:7 >>>> variance(cycles): 7; max_deviation: 32; min time: 36
 * loop_size:8 >>>> variance(cycles): 1; max_deviation: 16; min time: 40
 *
 * For cases 4 and 5, we notice quite quickly.  The for loop itself has some
 * overhead (probably more than our simple stores and adds).  So the resolution
 * of these methods is a little more than a loop's overhead.  For case 1, we
 * need about 7 loops, in addition to the overhead, until we can reliably detect
 * the additional instructions.  Note the deviation and variation increases for
 * all cases.
 *
 *
 * What about extra code before the measurement?  I reran the test cases with
 * some extra tsc-related code above the measurement (an accidental asm
 * insertion of lfence;rdtsc above reading the start time) and with no work in
 * between:
 * 		case 1: no effect
 * 		case 2: no effect
 * These both had some form of serialization (cpuid or lfence) above the rdtsc
 * command.  But when we try using just rdtscp (with no extra serialization:)
 * 		case 3, normal: lines   0 var, 0 max dev, 32 min, 0 var 0 max dev
 * 		case 3, extras: lines 2-3 var, 4 max dev, 28 min, 2 var 4 max dev
 * Similar deal with case 4.  Lots of 28s and deviation.  It looks like some
 * times the rdtsc diff is only 28, and others 32 (hence the deviation of 4).
 * Note this means the measurement interval is *lower*, which means the code was
 * *faster*.  Was the rdtscp not serializing instructions from above (which
 * doesn't make sense, since anything sneaking in from above should make the
 * code *slower*)?  Or is it because the previous command was rdtsc, which might
 * 'speed up' subsequent rdtscs.  I tried it again, with a little work between
 * the unused TSC read and the start tsc read:
 * 		case 3, more crap : lines 2-3 var, 4 max dev, 28 min, 2 var 4 max dev
 * So no real change from adding minor code in between.  What about adding an
 * lfence above the rdtscp (so it is almost exactly like case 2)?
 * Our assembly code now looks like:
 * 		lfence;
 * 		rdtsc;
 * 		mov %edx, (memory); 	// these get overwritten
 * 		mov %eax, (memory); 	// these get overwritten
 *
 * 		mov (memory), %eax;		// misc work (variable = i + j)
 * 		add %esi, %eax;			// misc work (variable = i + j)
 * 		mov %eax, (memory);		// misc work (variable = i + j)
 *
 * 		lfence;
 * 		rdtscp;					// this is the real start measurement
 * 		mov %edx, (memory);
 * 		mov %eax, (memory);
 *
 *      // no extra work here
 *
 * 		rdtscp;					// this is the real end measurement
 * 		mov %edx, (memory);
 * 		mov %eax, (memory);
 * 		cpuid;					// this is case 3, with sync after
 *
 * Even with this extra lfence, case 3-style still shows numbers like:
 * 		case 3, added crap: lines 2-3 var, 4 max dev, 28 min, 2 var 4 max dev
 * So either rdtscp is somehow faster due to internal-processor-caching (a
 * previous rdtsc makes the next rdtscp somewhat faster sometimes, including
 * after some instructions and an lfence), or the baseline case of no variation
 * is "wrong", and we really should expect between 28 and 32.  FWIW, the Intel
 * author also had a max deviation of 4 (per line).  And remember, on rare
 * occasions we get a 28 for case 3 and 4 (the other 9999999 times it is 32).
 *
 * Note how the modified case 3 is pretty much the same *in performance* as a
 * case 5.  But its code is nearly identical to case 2.  If you change the start
 * measurement's rdtscp to an rdtsc, the min goes from 28 -> 44 (this is case
 * 2).  And if you change the end measurements rdtscp to an lfence; rdtscp, we
 * go from 44->48 (this is no case).  Then if you change that rdtscp to an
 * rdtsc, we drop from 48->28 (this is case 5).  Based on this, it looks like
 * the different types of rdtsc take their time measurement at different points
 * within their execution.  rdtsc probably takes its measurement earlier in the
 * instruction (~16-20 cycles/ticks earlier perhaps?), based on the 48->28
 * back-side step and the front-side 28->44 step.
 * 		
 * Anyway, what matters is a relatively stable method without a lot of variance
 * that has a solid floor/min that we can detect at runtime (to run tests on a
 * given machine).  Using rdtscp for the start measurement seems unreliable
 * (when run alone we get 32, when run with things we get 28, on the corei7).
 * So even though case 3 and 4 had nice low variances and deviations, I don't
 * trust it, and would rather go with something that always gives me the same
 * result (as well as being a low result).  So case 5 will be my go-to for now.
 * It should have the same protection as the others (perhaps 6 is better), it is
 * stable, and it has a low overhead and low resolution (less capacity to hide
 * instruction latency).  Finally, the start and end measurements use the same
 * code, which is very convenient.
 *
 * This isn't conclusive - we'd need to do more tests with different workloads
 * on different machines, and probably talk to an intel architect.
 *
 * Still reading?  There's one more thing: System Management Mode!  This is an
 * interrupt context that is invisible to the OS, but we can see its effects in
 * our measurements.  If you run this code with the default settings, you often
 * won't see it (unless you have some loops).  However, if you run with
 * 1024x16384 (0x400 by 0x4000), you are likely to see very large max
 * deviations, such as 100, 600, or even 1500000.  From what I can tell, the
 * likelihood depends on how long the inner loop.  Using case 5 at 0x400,
 * 0x4000, after 3-4 runs, I had one line out of 1024 lines that was much
 * higher.  Three were 112, one was 1659260.  AFAIK, this is system management
 * mode kicking in.  You can mitigate this by disabling all types of USB legacy
 * support in the BIOS.  Specifically, faking USB keyboards and mice (making
 * them look like PS/2) and USB mass storage (making them look like a HDD) all
 * lead to an increase in SMIs.  For more info, check out:
 * 		https://rt.wiki.kernel.org/index.php/HOWTO:_Build_an_RT-application
 * It is not sufficient to merely not use things like the USB mass storage.  It
 * needs to be disabled in the BIOS.  At least, this is true on my nehalem.  A
 * while back, we had an issue with microbenchmarks taking 10% longer if you
 * held down a key on the keyboard, even if the code was running on a core that
 * did not receive the keyboard IRQ.  Turns out this was due to a USB keyboard
 * in legacy mode.  The real root of this problem was SMM, which forces all
 * cores to enter SMM whenever any core enters SMM (hence the cross-core
 * interference).
 *
 * So finally, disable anything that may lead to SMM interference.  I have some
 * code that runs at startup that tries to determine the min time for the given
 * approved method of measurement (i.e., case 5), and also tries to detect SMIs
 * via massive latency spikes.  */

#include <ros/common.h>
#include <arch/arch.h>
#include <stdio.h>
#include <kmalloc.h>
#include <time.h>

#define STAT_SIZE_DEF 10000
#define LOOP_BOUND_DEF 1000

/* Fills in the **times with the results of the double loop measurement.  There
 * are many options for start and end time measurements, all inside #if 0 #endif
 * comments.  Copy/paste whichever you'd like to test out. */
static inline void filltimes(uint64_t **times, unsigned int loop_bound,
                             unsigned int stat_size)
{
	unsigned long flags;
	int i, j;
	uint64_t start, end;
	unsigned int start_low, start_high, end_low, end_high;
	unsigned int dummy_low, dummy_high;
	volatile int variable = 0;
	int8_t state = 0;

	/* Variety of warmups.  recommended for cpuid... */
	asm volatile ("cpuid\n\t"
	              "rdtsc\n\t"
	              "cpuid\n\t"
	              "rdtsc\n\t"
	              "cpuid\n\t"
	              "rdtsc\n\t"
	              "mov %%edx, %0\n\t"
	              "mov %%eax, %1\n\t": "=m" (dummy_high), "=m" (dummy_low)::
	              "%eax", "%ebx", "%ecx", "%edx");
	for (j = 0; j < loop_bound; j++) {
		for (i = 0; i < stat_size; i++) {
			variable = 0;
			/* starting side, i want to make sure we always copy out to memory
			 * (stack), instead of sometimes using registers (and other times
			 * not).  if you use =a, for instance, with no work, the compiler
			 * will use esi and edi to store start_high and _low.
			 *
			 * The same concern is probably unnecessary at the end, but it might
			 * keep the compiler from reserving the use of those registers.*/

			#if 0 /* extra crap before the measurement code */
			asm volatile (
						  "lfence;"
			              "rdtsc;"
						  "mov %%edx, %0;"
						  "mov %%eax, %1;"
						  : "=m" (dummy_high), "=m" (dummy_low)
						  :
						  : "%eax", "%edx");

			variable = i + j;
			#endif

			asm volatile (
						  "lfence;"
			              "rdtsc;"
						  "mov %%edx, %0;"
						  "mov %%eax, %1;"
						  : "=m" (start_high), "=m" (start_low)
						  :
						  : "%eax", "%edx");
			#if 0 	/* types of start time measurements */
			asm volatile (
			              "cpuid;"
			              "rdtsc;"
						  "mov %%edx, %0;"
						  "mov %%eax, %1;"
						  : "=m" (start_high), "=m" (start_low)
						  :
						  : "%eax", "%ebx", "%ecx", "%edx");
			asm volatile (
						  "lfence;"
			              "rdtsc;"
						  "mov %%edx, %0;"
						  "mov %%eax, %1;"
						  : "=m" (start_high), "=m" (start_low)
						  :
						  : "%eax", "%edx");
			asm volatile (
						  "lfence;"
			              "rdtsc;"
						  "lfence;"
						  "mov %%edx, %0;"
						  "mov %%eax, %1;"
						  : "=m" (start_high), "=m" (start_low)
						  :
						  : "%eax", "%edx");

			asm volatile(
			             "rdtscp;"
						  "mov %%edx, %0;"
						  "mov %%eax, %1;"
						 : "=m" (start_high), "=m" (start_low)
						 :
						 : "%eax", "%ecx", "%edx");
			#endif

			/* call the function to measure here */

			#if 0 /* some options for code to measure */
			variable = j;

			variable = i + j;

			for (int k = 0; k < j; k++)
				variable = k;
			#endif

			asm volatile("lfence;"
			             "rdtsc;"
			             "mov %%edx, %0;"
			             "mov %%eax, %1;"
						 : "=m" (end_high), "=m" (end_low)
						 :
						 : "%eax", "%edx");
			#if 0 	/* types of end time measurements */
			asm volatile("cpuid;"
			             "rdtsc;"
			             "mov %%edx, %0;"
			             "mov %%eax, %1;"
						 : "=m" (end_high), "=m" (end_low)
						 :
						 : "%eax", "%ebx", "%ecx", "%edx");
			asm volatile("lfence;"
			             "rdtsc;"
			             "mov %%edx, %0;"
			             "mov %%eax, %1;"
						 : "=m" (end_high), "=m" (end_low)
						 :
						 : "%eax", "%edx");
			asm volatile("lfence;"
			             "rdtsc;"
						  "lfence;"
			             "mov %%edx, %0;"
			             "mov %%eax, %1;"
						 : "=m" (end_high), "=m" (end_low)
						 :
						 : "%eax", "%edx");

			asm volatile(
			             "rdtscp;"
			             "mov %%edx, %0;"
			             "mov %%eax, %1;"
						 : "=m" (end_high), "=m" (end_low)
						 :
						 : "%eax", "%ecx", "%edx");
			asm volatile(
			             "rdtscp;"
						 "lfence;"
			             "mov %%edx, %0;"
			             "mov %%eax, %1;"
						 : "=m" (end_high), "=m" (end_low)
						 :
						 : "%eax", "%ecx", "%edx");
			asm volatile(
			             "rdtscp;"
			             "mov %%edx, %0;"
			             "mov %%eax, %1;"
			             "cpuid;"
						 : "=m" (end_high), "=m" (end_low)
						 :
						 : "%eax", "%ebx", "%ecx", "%edx");
			#endif
			
			start = ( ((uint64_t)start_high << 32) | start_low );
			end = ( ((uint64_t)end_high << 32) | end_low );
			
			if ( (int64_t)(end - start) < 0) {
				printk("CRITICAL ERROR IN TAKING THE TIME!!!!!!\n"
                       "loop(%d) stat(%d) start = %llu, end = %llu, "
                       "variable = %u\n", j, i, start, end, variable);
				times[j][i] = 0;
			} else {
				times[j][i] = end - start;
			}
		}
	}
}

/* http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance, doing pop
 * variance, multiplying by N/N, and not checking overflow of size*size */
uint64_t var_calc(uint64_t *inputs, int size)
{
	int i;
	uint64_t acc = 0, previous = 0, temp_var = 0;
	for (i = 0; i < size; i++) {
		if (acc < previous)
			goto overflow;
		previous = acc;
		acc += inputs[i];
	}
	acc = acc * acc;
	if (acc < previous)
		goto overflow;
	previous = 0;
	for (i = 0; i < size; i++) {
		if (temp_var < previous)
			goto overflow;
		previous = temp_var;
		temp_var+= (inputs[i]*inputs[i]);
	}
	temp_var = temp_var * size;
	if (temp_var < previous)
		goto overflow;
	temp_var = (temp_var - acc)/(((uint64_t)(size))*((uint64_t)(size)));
	return (temp_var);
overflow:
	printk("CRITICAL OVERFLOW ERROR IN var_calc!!!!!!\n\n");
	return -1;
}

int test_rdtsc(unsigned int loop_bound, unsigned int stat_size)
{
	int8_t state = 0;

	int i = 0, j = 0, spurious = 0, k = 0;
	uint64_t **times;
	uint64_t *variances;
	uint64_t *min_values;
	uint64_t max_dev = 0, min_time = 0, max_time = 0, prev_min = 0;
	uint64_t tot_var = 0, max_dev_all = 0, var_of_vars = 0, var_of_mins = 0;
	loop_bound = loop_bound ?: LOOP_BOUND_DEF;
	stat_size = stat_size ?: STAT_SIZE_DEF;
	
	printk("Running rdtsc tests...\n");
	
	times = kmalloc(loop_bound * sizeof(uint64_t*), 0);
	if (!times) {
		printk("unable to allocate memory for times\n");
		return 0;
	}

	for (j = 0; j < loop_bound; j++) {
		times[j] = kmalloc(stat_size * sizeof(uint64_t), 0);
		if (!times[j]) {
			printk("unable to allocate memory for times[%d]\n", j);
			for (k = 0; k < j; k++)
				kfree(times[k]);
			return 0;
		}
	}
	
	variances = kmalloc(loop_bound * sizeof(uint64_t), 0);
	if (!variances) {
		printk("unable to allocate memory for variances\n");
		// not bothering to free **times
		return 0;
	}
	
	min_values = kmalloc(loop_bound * sizeof(uint64_t), 0);
	if (!min_values) {
		printk("unable to allocate memory for min_values\n");
		// not bothering to free **times or variances
		return 0;
	}
	
	disable_irqsave(&state);

	filltimes(times, loop_bound, stat_size);

	enable_irqsave(&state);

	for (j = 0; j < loop_bound; j++) {
		max_dev = 0;
		min_time = 0;
		max_time = 0;
	
		for (i = 0; i < stat_size; i++) {
			if ((min_time == 0) || (min_time > times[j][i]))
				min_time = times[j][i];
			if (max_time < times[j][i])
				max_time = times[j][i];
		}
		max_dev = max_time - min_time;
		min_values[j] = min_time;
		if ((prev_min != 0) && (prev_min > min_time))
			spurious++;
		if (max_dev > max_dev_all)
			max_dev_all = max_dev;
		variances[j] = var_calc(times[j], stat_size);
		tot_var += variances[j];
		
		printk("loop_size:%d >>>> variance(cycles): %llu; "
               "max_deviation: %llu; min time: %llu\n", j, variances[j],
               max_dev, min_time);
		prev_min = min_time;
	}
	
	var_of_vars = var_calc(variances, loop_bound);
	var_of_mins = var_calc(min_values, loop_bound);
	
	printk("total number of spurious min values = %d\n", spurious);
	/* is this next one the mean variance, not the total? */
	printk("total variance = %llu\n", (tot_var/loop_bound));
	printk("absolute max deviation = %llu\n", max_dev_all);
	printk("variance of variances = %llu\n", var_of_vars);
	printk("variance of minimum values = %llu\n", var_of_mins);
	
	for (j = 0; j < loop_bound; j++) {
		kfree(times[j]);
	}
	kfree(times);
	kfree(variances);
	kfree(min_values);
	return 0;
}


/* Crude SMI or other TSC-instability detection. */
bool check_timing_stability(void)
{
	uint64_t min_overhead = UINT64_MAX;
	uint64_t max_overhead = 0;
	uint64_t start, end, diff;
	uint32_t edx;
	int8_t irq_state = 0;
	volatile int dummy = 0;

	/* Don't even bother if we don't have an invariant TSC */
	cpuid(0x80000007, 0x0, 0, 0, 0, &edx);
	if (!(edx & (1 << 8))) {
		printk("Invariant TSC not present.  Do not benchmark!\n");
		return FALSE;
	}
	disable_irqsave(&irq_state);
	/* 2mil detected an SMI about 95% of the time on my nehalem. */
	for (int i = 0; i < 3000000; i++) {
		start = read_tsc_serialized();
		for (int j = 0; j < 500; j++) 
			dummy = j;
 		end = read_tsc_serialized();
		if ((int64_t)(end - start) < 0) {
			printk("TSC stability overflow error!\n");
			return FALSE;
		}
		diff = end - start;
		min_overhead = MIN(min_overhead, diff);
		max_overhead = MAX(max_overhead, diff);
	}
	enable_irqsave(&irq_state);
	if (max_overhead - min_overhead > 50) {
		printk("Test TSC overhead unstable (Min: %llu, Max: %llu).  "
		       "Do not benchmark!\n", min_overhead, max_overhead);
		return FALSE;
	}
	return TRUE;
}

void test_tsc_cycles(void)
{
	uint64_t start, end;
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	start = read_tsc_serialized();
	for (int i = 0; i < 1000; i++) {
		asm volatile ("addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
	                  "addl $1, %%eax;"
				      : : : "eax", "cc");
	}
	end = read_tsc_serialized();
	end = end - start - system_timing.timing_overhead;
	printk("%llu (100,000) ticks passed, run twice to load the icache\n", end);

	enable_irqsave(&irq_state);
}
