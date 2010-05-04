/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */


#ifdef __SHARC__
#pragma nosharc
#endif

#include <ros/common.h>
#include <smp.h>
#include <arch/init.h>
#include <mm.h>
#include <elf.h>
#include <frontend.h>

#include <kmalloc.h>
#include <assert.h>
#include <manager.h>
#include <process.h>
#include <schedule.h>
#include <syscall.h>
#include <testing.h>
#include <kfs.h>
#include <stdio.h>
#include <timing.h>
#include <resource.h>
#include <monitor.h>
#include <colored_caches.h>
#include <string.h>
#include <pmap.h>
#include <ros/timer.h>
#include <ros/arch/membar.h>

/*
 * Currently, if you leave this function by way of proc_run (process_workqueue
 * that proc_runs), you will never come back to where you left off, and the
 * function will start from the top.  Hence the hack 'progress'.
 */
void manager(void)
{
	#ifndef DEVELOPER_NAME
		#define DEVELOPER_NAME brho
	#endif

	// LoL
	#define PASTE(s1,s2) s1 ## s2
	#define MANAGER_FUNC(dev) PASTE(manager_,dev)

	void MANAGER_FUNC(DEVELOPER_NAME)(void);
	MANAGER_FUNC(DEVELOPER_NAME)();
}

/* Helper macro for quickly running something out of KFS.  Pass it a string and
 * a proc pointer. */
#define quick_proc_run(x, p)                                                     \
	(p) = kfs_proc_create(kfs_lookup_path((x)));                                 \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);                                                \
	proc_run((p));                                                               \
	proc_decref((p), 1);

#define quick_proc_create(x, p)                                                  \
	(p) = kfs_proc_create(kfs_lookup_path((x)));                                 \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);

#define quick_proc_color_run(x, p, c)                                            \
	(p) = kfs_proc_create(kfs_lookup_path((x)));                                 \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);                                                \
	p->cache_colors_map = cache_colors_map_alloc();                              \
	for (int i = 0; i < (c); i++)                                                \
		cache_color_alloc(llc_cache, p->cache_colors_map);                       \
	proc_run((p));                                                               \
	proc_decref((p), 1);

#define quick_proc_color_create(x, p, c)                                         \
	(p) = kfs_proc_create(kfs_lookup_path((x)));                                 \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);                                                \
	p->cache_colors_map = cache_colors_map_alloc();                              \
	for (int i = 0; i < (c); i++)                                                \
		cache_color_alloc(llc_cache, p->cache_colors_map);

void manager_brho(void)
{
	static uint8_t RACY progress = 0;
	static struct proc *p;

	// for testing taking cores, check in case 1 for usage
	uint32_t corelist[MAX_NUM_CPUS];
	uint32_t num = 3;

	switch (progress++) {
		case 0:
			/* 124 is half of the available boxboro colors (with the kernel
			 * getting 8) */
			quick_proc_color_run("msr_dumb_while", p, 124);
			//quick_proc_run("msr_dumb_while", p);
			#if 0
			// this is how you can transition to a parallel process manually
			// make sure you don't proc run first
			__proc_set_state(p, PROC_RUNNING_S);
			__proc_set_state(p, PROC_RUNNABLE_M);
			p->resources[RES_CORES].amt_wanted = 5;
			spin_unlock(&p->proc_lock);
			core_request(p);
			panic("This is okay");
			#endif
			break;
		case 1:
			#if 0
			udelay(10000000);
			// this is a ghetto way to test restarting an _M
				printk("\nattempting to ghetto preempt...\n");
				spin_lock(&p->proc_lock);
				proc_take_allcores(p, __death);
				__proc_set_state(p, PROC_RUNNABLE_M);
				spin_unlock(&p->proc_lock);
				udelay(5000000);
				printk("\nattempting to restart...\n");
				core_request(p); // proc still wants the cores
			panic("This is okay");
			// this tests taking some cores, and later killing an _M
				printk("taking 3 cores from p\n");
				for (int i = 0; i < num; i++)
					corelist[i] = 7-i; // 7, 6, and 5
				spin_lock(&p->proc_lock);
				proc_take_cores(p, corelist, &num, __death);
				spin_unlock(&p->proc_lock);
				udelay(5000000);
				printk("Killing p\n");
				proc_destroy(p);
				printk("Killed p\n");
			panic("This is okay");

			envs[0] = kfs_proc_create(kfs_lookup_path("roslib_hello"));
			__proc_set_state(envs[0], PROC_RUNNABLE_S);
			proc_run(envs[0]);
			break;
			#endif
		case 2:
			/*
			test_smp_call_functions();
			test_checklists();
			test_barrier();
			test_print_info();
			test_lapic_status_bit();
			test_ipi_sending();
			test_pit();
			*/
		default:
			printd("Manager Progress: %d\n", progress);
			// delay if you want to test rescheduling an MCP that yielded
			//udelay(15000000);
			schedule();
	}
	panic("If you see me, then you probably screwed up");
	monitor(0);

	/*
	printk("Servicing syscalls from Core 0:\n\n");
	while (1) {
		process_generic_syscalls(&envs[0], 1);
		cpu_relax();
	}
	*/
	return;
}

void manager_klueska()
{
	static struct proc *envs[256];
	static volatile uint8_t progress = 0;

	if (progress == 0) {
		progress++;
		envs[0] = kfs_proc_create(kfs_lookup_path("fillmeup"));
		__proc_set_state(envs[0], PROC_RUNNABLE_S);
		proc_run(envs[0]);
	}
	schedule();

	panic("DON'T PANIC");
}

struct elf_info
{
	long entry;
	long phdr;
	int phnum;
	int dynamic;
	char interp[256];
};

void manager_waterman()
{
	static int init = 0;
	if(!init)
	{
		init = 1;
		struct proc* p = proc_create(NULL,0);

		char* argv[] = {"/bin/sh","-l",0};
		char* envp[] = {"LD_LIBRARY_PATH=/lib",0};
		procinfo_pack_args(p->procinfo,argv,envp);

		struct file* f = file_open("/bin/busybox",0,0);
		assert(f != NULL);
		assert(load_elf(p,f) == 0);
		file_decref(f);

		__proc_set_state(p, PROC_RUNNABLE_S);
		proc_run(p);
	}
	schedule();
}

void manager_pearce()
{
	static struct proc *envs[256];
	static volatile uint8_t progress = 0;

	if (progress == 0) {
		progress++;
		envs[0] = kfs_proc_create(kfs_lookup_path("parlib_httpserver_integrated"));
		//envs[0] = kfs_proc_create(kfs_lookup_path("parlib_lock_test"));
		__proc_set_state(envs[0], PROC_RUNNABLE_S);
		proc_run(envs[0]);
	}
	schedule();

	panic("DON'T PANIC");

}

#ifdef __CONFIG_OSDI__
/* Manager for Micro benchmarks, OSDI, etc */
struct proc *mgr_p1 = 0;
struct proc *mgr_p2 = 0;
static void exper_1_part2(struct proc **pp);
static void exper_2_part2(struct proc **pp);
static void exper_3_part2(struct proc **pp);
static void exper_4_part2(struct proc **pp);
static void exper_5_part2(struct proc **pp);
static void exper_6_part2(struct proc **pp);
static void exper_7_part2(struct proc **pp);
static void exper_8_part2(struct proc **pp);
static void exper_9_part2(struct proc **pp);

void manager_tests(void)
{
	static uint8_t RACY progress = 0;

	printk("Test Progress: %d\n", progress);
	/* 10 runs of every experiment.  Finishing/Part2 is harmless on a null
	 * pointer.  We need to clean up/ finish/ part2 after each quick_proc_run,
	 * since we leave the monitor and only enter on another run (with
	 * progress++).  That's why we run a part2 in the first case: of the next
	 * experiment. */
	switch (progress++) {
		/* Experiment 1: get max vcores */
		case 0:
			printk("************* Starting experiment 1 ************** \n");
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			exper_1_part2(&mgr_p1);
			quick_proc_run("msr_get_cores", mgr_p1);
			break;
		/* Experiment 2: get a single vcore */
		case 10:
			exper_1_part2(&mgr_p1);
			printk("************* Starting experiment 2 ************** \n");
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
		case 18:
		case 19:
			exper_2_part2(&mgr_p1);
			quick_proc_run("msr_get_singlecore", mgr_p1);
			break;
		/* Experiment 3: kill a _M */
		case 20: /* leftover from exp 2 */
			exper_2_part2(&mgr_p1);
			printk("************* Starting experiment 3 ************** \n");
		case 21:
		case 22:
		case 23:
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
			exper_3_part2(&mgr_p1);
			quick_proc_run("msr_dumb_while", mgr_p1);
			break;
		/* Experiment 4: _S create and death*/
		case 30: /* leftover from exp 3 */
			exper_3_part2(&mgr_p1);
			printk("************* Starting experiment 4 ************** \n");
		case 31:
		case 32:
		case 33:
		case 34:
		case 35:
		case 36:
		case 37:
		case 38:
		case 39:
			exper_4_part2(&mgr_p1);
			printk("[T]:004:S:%llu\n", read_tsc());
			quick_proc_run("tsc_spitter", mgr_p1);
			break;
		/* Experiment 5: raw preempt, entire process*/
		case 40:
			exper_4_part2(&mgr_p1);
			printk("************* Starting experiment 5 ************** \n");
		case 41:
		case 42:
		case 43:
		case 44:
		case 45:
		case 46:
		case 47:
		case 48:
		case 49:
			exper_5_part2(&mgr_p1);
			quick_proc_run("msr_nice_while", mgr_p1);
			break;
		/* Experiment 6: preempt-warn, entire process */
		case 50:
			exper_5_part2(&mgr_p1);
			printk("************* Starting experiment 6 ************** \n");
		case 51:
		case 52:
		case 53:
		case 54:
		case 55:
		case 56:
		case 57:
		case 58:
		case 59:
			exper_6_part2(&mgr_p1);
			quick_proc_run("msr_nice_while", mgr_p1);
			break;
		/* Experiment 7: preempt-raw, single core */
		case 60:
			exper_6_part2(&mgr_p1);
			printk("************* Starting experiment 7 ************** \n");
		case 61:
		case 62:
		case 63:
		case 64:
		case 65:
		case 66:
		case 67:
		case 68:
		case 69:
			exper_7_part2(&mgr_p1);
			quick_proc_run("msr_nice_while", mgr_p1);
			break;
		/* Experiment 8: preempt-warn, single core */
		case 70:
			exper_7_part2(&mgr_p1);
			printk("************* Starting experiment 8 ************** \n");
		case 71:
		case 72:
		case 73:
		case 74:
		case 75:
		case 76:
		case 77:
		case 78:
		case 79:
			exper_8_part2(&mgr_p1);
			quick_proc_run("msr_nice_while", mgr_p1);
			break;
		/* Experiment 9: single notification time */
		case 80:
			exper_8_part2(&mgr_p1);
			printk("************* Starting experiment 9 ************** \n");
		case 81:
		case 82:
		case 83:
		case 84:
		case 85:
		case 86:
		case 87:
		case 88:
		case 89:
			exper_9_part2(&mgr_p1);
			quick_proc_run("msr_dumb_while", mgr_p1);
			break;
		/* Experiment 10: cycling vcore */
		case 90:
			exper_9_part2(&mgr_p1);
			printk("************* Starting experiment 10 ************* \n");
			quick_proc_run("msr_dumb_while", mgr_p1);
			break;
		case 91:
			quick_proc_run("msr_cycling_vcores", mgr_p2);
			break;
		case 92:
			printk("Will go on forever.  Udelaying for two minutes.\n");
			udelay(120000000);
			proc_incref(mgr_p1, 1);
			proc_destroy(mgr_p1);
			proc_decref(mgr_p1, 1);
			proc_incref(mgr_p2, 1);
			proc_destroy(mgr_p2);
			proc_decref(mgr_p2, 1);
			printk("Done with the tests!");
			monitor(0);
			break;
		default:
			printd("Manager Progress: %d\n", progress);
			schedule();
	}
	monitor(0);
	return;
}

/* OSDI experiment "bottom halves" */
/* Experiment 1: get max vcores */
static void exper_1_part2(struct proc **pp)
{
	while (*pp) /* make sure the previous run is over */
		cpu_relax();
}

/* Experiment 2: get a single vcore */
static void exper_2_part2(struct proc **pp)
{
	while (*pp) /* make sure the previous run is over */
		cpu_relax();
}

/* Experiment 3: kill a _M */
static void exper_3_part2(struct proc **pp)
{
	uint64_t begin = 0, diff = 0;

	if (*pp) { /* need to kill, etc */
		proc_incref(*pp, 1);
		begin = start_timing();	
		proc_destroy(*pp);
		proc_decref(*pp, 1);
		wmb();
		while (*pp) /* toggled in proc_free */
			cpu_relax();
		diff = stop_timing(begin);	
		printk("Took %llu usec (%llu nsec) to kill.\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		printk("[T]:003:%llu:%llu\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
	}
}

/* Experiment 4: _S create and death*/
static void exper_4_part2(struct proc **pp)
{
	while (*pp) /* make sure the previous run is over */
		cpu_relax();
}

/* Experiment 5: raw preempt, entire process*/
static void exper_5_part2(struct proc **pp)
{
	uint64_t begin = 0, diff = 0;
	uint32_t end_refcnt = 0;
	bool self_ipi_pending = FALSE;

	if (*pp) {
		proc_incref(*pp, 1);
		spin_lock(&(*pp)->proc_lock);
		end_refcnt = (*pp)->env_refcnt - (*pp)->procinfo->num_vcores;
		begin = start_timing();
		self_ipi_pending = __proc_preempt_all(*pp);
		spin_unlock(&(*pp)->proc_lock);
		__proc_kmsg_pending(*pp, self_ipi_pending);
		spin_on((*pp)->env_refcnt != end_refcnt);
		diff = stop_timing(begin);
		printk("Took %llu usec (%llu nsec) to raw preempt all.\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		printk("[T]:005:%llu:%llu\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		proc_destroy(*pp);
		proc_decref(*pp, 1);
		while (*pp) /* toggled in proc_free */
			cpu_relax();
	}
}

/* Experiment 6: preempt-warn, entire process */
static void exper_6_part2(struct proc **pp)
{
	uint64_t begin = 0, diff = 0;

	if (*pp) {
		proc_incref(*pp, 1);
		spin_lock(&(*pp)->proc_lock);
		begin = start_timing();
		__proc_preempt_warnall(*pp, 1000000);
		spin_unlock(&(*pp)->proc_lock);
		spin_on((*pp)->procinfo->num_vcores > 1);
		diff = stop_timing(begin);
		printk("Took %llu usec (%llu nsec) to warn preempt all.\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		printk("[T]:006:%llu:%llu\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		proc_destroy(*pp);
		proc_decref(*pp, 1);
		while (*pp) /* toggled in proc_free */
			cpu_relax();
	}
}

/* Experiment 7: preempt-raw, single core */
static void exper_7_part2(struct proc **pp)
{
	uint64_t begin = 0, diff = 0;
	bool self_ipi_pending = FALSE;
	uint32_t vcoreid, pcoreid = 7; // some core available on all systems

	if (*pp) {
		proc_incref(*pp, 1);
		spin_lock(&(*pp)->proc_lock);
		assert((*pp)->procinfo->pcoremap[pcoreid].valid);
		begin = start_timing();
		self_ipi_pending = __proc_preempt_core(*pp, pcoreid);
		spin_unlock(&(*pp)->proc_lock);
		__proc_kmsg_pending(*pp, self_ipi_pending);
		spin_on((*pp)->procinfo->pcoremap[pcoreid].valid);
		diff = stop_timing(begin);
		printk("Took %llu usec (%llu nsec) to raw-preempt one core.\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		printk("[T]:007:%llu:%llu\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		proc_destroy(*pp);
		proc_decref(*pp, 1);
		while (*pp) /* toggled in proc_free */
			cpu_relax();
	}
}

/* Experiment 8: preempt-warn, single core */
static void exper_8_part2(struct proc **pp)
{
	uint64_t begin = 0, diff = 0;
	uint32_t vcoreid, pcoreid = 7; // some core available on all systems

	if (*pp) {
		proc_incref(*pp, 1);
		spin_lock(&(*pp)->proc_lock);
		vcoreid = (*pp)->procinfo->pcoremap[pcoreid].vcoreid;
		assert((*pp)->procinfo->pcoremap[pcoreid].valid);
		begin = start_timing();
		__proc_preempt_warn(*pp, vcoreid, 1000000); // 1 sec
		spin_unlock(&(*pp)->proc_lock);
		spin_on((*pp)->procinfo->pcoremap[pcoreid].valid);
		diff = stop_timing(begin);
		printk("Took %llu usec (%llu nsec) to warn-preempt one core.\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		printk("[T]:008:%llu:%llu\n",
		       diff * 1000000 / system_timing.tsc_freq,
		       diff * 1000000000 / system_timing.tsc_freq);
		proc_destroy(*pp);
		proc_decref(*pp, 1);
		while (*pp) /* toggled in proc_free */
			cpu_relax();
	}
}

/* Experiment 9: single notification time */
static void exper_9_part2(struct proc **pp)
{
	struct notif_event ne = {0};

	if (*pp) {
		ne.ne_type = NE_ALARM;
		proc_incref(*pp, 1);
		printk("[T]:009:B:%llu\n", read_tsc());
		proc_notify(*pp, NE_ALARM, &ne); 
		proc_destroy(*pp);
		proc_decref(*pp, 1);
		while (*pp) /* toggled in proc_free */
			cpu_relax();
	}
}

#endif /* __CONFIG_OSDI__ */

#ifdef __sparc_v8__

static char*
itoa(int num, char* buf0, size_t base)
{
	if(base > 16)
		return NULL;

	char* buf = buf0;
	int len = 0, i;

	if(num < 0)
	{
		*buf++ = '-';
		num = -num;
	}

	do {
		buf[len++] = "0123456789abcdef"[num%base];
		num /= base;
	} while(num);

	for(i = 0; i < len/2; i++)
	{
		char temp = buf[i];
		buf[i] = buf[len-i-1];
		buf[len-i-1] = temp;
	}
	buf[len] = 0;

	return buf0;
}

void gsf_set_frame_cycles(int cycles)
{
	store_alternate(26*4,2,cycles);
}

void gsf_set_partition_credits(int partition, int credits)
{
	store_alternate((32+partition)*4,2,credits);
}

void gsf_set_core_partition(int core, int partition)
{
	store_alternate((64+core)*4,2,partition);
}

#endif

