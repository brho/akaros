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

char *p_argv[] = {0, 0, 0};
char *p_envp[] = {"LD_LIBRARY_PATH=/lib", 0};
/* Helper macro for quickly running a process.  Pass it a string, *file, and a
 * *proc. */
#define quick_proc_run(x, p, f)                                                  \
	(f) = do_file_open((x), 0, 0);                                               \
	assert((f));                                                                 \
	p_argv[0] = file_name((f));                                                  \
	(p) = proc_create((f), p_argv, p_envp);                                      \
	kref_put(&(f)->f_kref);                                                      \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);                                                \
	proc_run((p));                                                               \
	proc_decref((p));

#define quick_proc_create(x, p, f)                                               \
	(f) = do_file_open((x), 0, 0);                                               \
	assert((f));                                                                 \
	p_argv[0] = file_name((f));                                                  \
	(p) = proc_create((f), p_argv, p_envp);                                      \
	kref_put(&(f)->f_kref);                                                      \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);

#define quick_proc_color_run(x, p, c, f)                                         \
	(f) = do_file_open((x), 0, 0);                                               \
	assert((f));                                                                 \
	p_argv[0] = file_name((f));                                                  \
	(p) = proc_create((f), p_argv, p_envp);                                      \
	kref_put(&(f)->f_kref);                                                      \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);                                                \
	p->cache_colors_map = cache_colors_map_alloc();                              \
	for (int i = 0; i < (c); i++)                                                \
		cache_color_alloc(llc_cache, p->cache_colors_map);                       \
	proc_run((p));                                                               \
	proc_decref((p));

#define quick_proc_color_create(x, p, c, f)                                      \
	(f) = do_file_open((x), 0, 0);                                               \
	assert((f));                                                                 \
	p_argv[0] = file_name((f));                                                  \
	(p) = proc_create((f), p_argv, p_envp);                                      \
	kref_put(&(f)->f_kref);                                                      \
	spin_lock(&(p)->proc_lock);                                                  \
	__proc_set_state((p), PROC_RUNNABLE_S);                                      \
	spin_unlock(&(p)->proc_lock);                                                \
	p->cache_colors_map = cache_colors_map_alloc();                              \
	for (int i = 0; i < (c); i++)                                                \
		cache_color_alloc(llc_cache, p->cache_colors_map);

void manager_brho(void)
{
	static uint8_t RACY progress = 0;	/* this will wrap around. */
	static struct proc *p;
	struct file *temp_f;

	/* I usually want this */
	schedule();
	process_routine_kmsg(0);	/* maybe do this before schedule() */

	enable_irq();
	/* this ghetto hack really wants to wait for an interrupt, but time out */
	udelay(60000);	/* wait for IO when there really is nothing to do */
	process_routine_kmsg(0);
	printd("No work to do (schedule returned)\n");
	monitor(0);

	// for testing taking cores, check in case 1 for usage
	uint32_t corelist[MAX_NUM_CPUS];
	uint32_t num = 3;

	switch (progress++) {
		case 0:
			printk("Top of the manager to ya!\n");
			/* 124 is half of the available boxboro colors (with the kernel
			 * getting 8) */
			//quick_proc_color_run("msr_dumb_while", p, 124, temp_f);
			quick_proc_run("/bin/hello", p, temp_f);
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
		panic("what do you want to do?");
		//envs[0] = kfs_proc_create(kfs_lookup_path("fillmeup"));
		__proc_set_state(envs[0], PROC_RUNNABLE_S);
		proc_run(envs[0]);
	}
	schedule();

	panic("DON'T PANIC");
}

void manager_waterman()
{
	static int init = 0;
	if(!init)
	{
		warn("Old file creation might not work");
		init = 1;
		struct proc *p;
		proc_alloc(&p, 0);

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
		panic("what do you want to do?");
		//envs[0] = kfs_proc_create(kfs_lookup_path("parlib_httpserver_integrated"));
		//envs[0] = kfs_proc_create(kfs_lookup_path("parlib_lock_test"));
		__proc_set_state(envs[0], PROC_RUNNABLE_S);
		proc_run(envs[0]);
	}
	schedule();

	panic("DON'T PANIC");

}

void manager_yuzhu()
{
	
	static uint8_t RACY progress = 0;
	static struct proc *p;

	// for testing taking cores, check in case 1 for usage
	uint32_t corelist[MAX_NUM_CPUS];
	uint32_t num = 3;

	//create_server(init_num_cores, loop);

	monitor(0);

	// quick_proc_run("hello", p);

}

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

