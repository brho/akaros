// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>
#include <stab.h>
#include <smp.h>
#include <arch/console.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <monitor.h>
#include <trap.h>
#include <pmap.h>
#include <kdebug.h>
#include <testing.h>
#include <manager.h>
#include <schedule.h>
#include <resource.h>
#include <kdebug.h>
#include <syscall.h>
#include <kmalloc.h>
#include <elf.h>
#include <event.h>

#include <ros/timer.h>
#include <ros/memlayout.h>
#include <ros/event.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line

typedef struct command {
	const char *NTS name;
	const char *NTS desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf);
} command_t;

static command_t (RO commands)[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Dump a backtrace", mon_backtrace },
	{ "reboot", "Take a ride to the South Bay", mon_reboot },
	{ "showmapping", "Shows VA->PA mappings", mon_showmapping},
	{ "setmapperm", "Sets permissions on a VA->PA mapping", mon_setmapperm},
	{ "cpuinfo", "Prints CPU diagnostics", mon_cpuinfo},
	{ "ps", "Prints process list", mon_ps},
	{ "nanwan", "Meet Nanwan!!", mon_nanwan},
	{ "bin_ls", "List files in /bin", mon_bin_ls},
	{ "bin_run", "Create and run a program from /bin", mon_bin_run},
	{ "manager", "Run the manager", mon_manager},
	{ "procinfo", "Show information about processes", mon_procinfo},
	{ "exit", "Leave the monitor", mon_exit},
	{ "kfunc", "Run a kernel function directly (!!!)", mon_kfunc},
	{ "notify", "Notify a process.  Vcoreid will skip their prefs", mon_notify},
	{ "measure", "Run a specific measurement", mon_measure},
	{ "trace", "Run a specific measurement", mon_trace},
	{ "monitor", "Run the monitor on another core", mon_monitor},
	{ "fs", "Filesystem Diagnostics", mon_fs},
	{ "bb", "Try to run busybox (ash)", mon_bb},
	{ "alarm", "Alarm Diagnostics", mon_alarm},
	{ "bcq", "BCQ Diagnostics", mon_bcq},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int mon_help(int argc, char **argv, trapframe_t *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int mon_ps(int argc, char** argv, trapframe_t *tf)
{
	print_allpids();
	return 0;
}

int mon_kerninfo(int argc, char **argv, trapframe_t *tf)
{
	extern char (RO SNT _start)[], (RO SNT etext)[], (RO SNT edata)[], (RO SNT end)[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, (uint32_t)(_start - KERNBASE));
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, (uint32_t)(etext - KERNBASE));
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, (uint32_t)(edata - KERNBASE));
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, (uint32_t)(end - KERNBASE));
	cprintf("Kernel executable memory footprint: %dKB\n",
		(uint32_t)(end-_start+1023)/1024);
	return 0;
}

#if 0
zra: not called
static char RO* function_of(uint32_t address)
{
	extern stab_t (RO stab)[], (RO estab)[];
	extern char (RO stabstr)[];
	stab_t* symtab;
	stab_t* best_symtab = 0;
	uint32_t best_func = 0;

	// ugly and unsorted
	for (symtab = stab; symtab < estab; symtab++) {
		// only consider functions, type = N_FUN
		if ((symtab->n_type == N_FUN) &&
		    (symtab->n_value <= address) &&
			(symtab->n_value > best_func)) {
			best_func = symtab->n_value;
			best_symtab = symtab;
		}
	}
	// maybe the first stab really is the right one...  we'll see.
	if (best_symtab == 0)
		return "Function not found!";
	return stabstr + best_symtab->n_strx;
}
#endif

int mon_backtrace(int argc, char **argv, trapframe_t *tf)
{
	backtrace();
	return 0;
}

int mon_reboot(int argc, char **argv, trapframe_t *tf)
{
	cprintf("[Scottish Accent]: She's goin' down, Cap'n!\n");
	reboot();

	// really, should never see this
	cprintf("Sigh....\n");
	return 0;
}

int mon_showmapping(int argc, char **argv, trapframe_t *tf)
{
	if (argc < 2) {
		cprintf("Shows virtual -> physical mappings for a virtual address range.\n");
		cprintf("Usage: showmapping START_ADDR [END_ADDR]\n");
		return 1;
	}
	pde_t* pgdir = (pde_t*)vpd;
	pte_t *pte, *pde;
	page_t* page;
	uintptr_t start, i;
	size_t size;
	start = ROUNDDOWN(strtol(argv[1], 0, 16), PGSIZE);
	size = (argc == 2) ? 1 : strtol(argv[2], 0, 16) - start;
	if (size/PGSIZE > 512) {
		cprintf("Not going to do this for more than 512 items\n");
		return 1;
	}

	show_mapping(start,size);
	return 0;
}

int mon_setmapperm(int argc, char **argv, trapframe_t *tf)
{
#ifndef __i386__
	cprintf("I don't support this call yet!\n");
	return 1;
#else
	if (argc < 3) {
		cprintf("Sets VIRT_ADDR's mapping's permissions to PERMS (in hex)\n");
		cprintf("Only affects the lowest level PTE.  To adjust the PDE, do the math.\n");
		cprintf("Be careful with this around UVPT, VPT, and friends.\n");
		cprintf("Usage: setmapperm VIRT_ADDR PERMS\n");
		return 1;
	}
	pde_t*COUNT(PTSIZE) pgdir = (pde_t*COUNT(PTSIZE))vpd;
	pte_t *pte, *pde;
	page_t* page;
	uintptr_t va;
	va = ROUNDDOWN(strtol(argv[1], 0, 16), PGSIZE);
	page = page_lookup(pgdir, (void*SNT)va, &pte);
	if (!page) {
		cprintf("No such mapping\n");
		return 1;
	}
	pde = &pgdir[PDX(va)];
	cprintf("   Virtual    Physical  Ps Dr Ac CD WT U W\n");
	cprintf("------------------------------------------\n");
	cprintf("%08p  %08p  %1d  %1d  %1d  %1d  %1d  %1d %1d\n", va, page2pa(page),
	       (*pte & PTE_PS) >> 7, (*pte & PTE_D) >> 6, (*pte & PTE_A) >> 5,
	       (*pte & PTE_PCD) >> 4, (*pte & PTE_PWT) >> 3, (*pte & *pde & PTE_U) >> 2,
	       (*pte & *pde & PTE_W) >> 1);
	*pte = PTE_ADDR(*pte) | (*pte & PTE_PS) |
	       (PGOFF(strtol(argv[2], 0, 16)) & ~PTE_PS ) | PTE_P;
	cprintf("%08p  %08p  %1d  %1d  %1d  %1d  %1d  %1d %1d\n", va, page2pa(page),
	       (*pte & PTE_PS) >> 7, (*pte & PTE_D) >> 6, (*pte & PTE_A) >> 5,
	       (*pte & PTE_PCD) >> 4, (*pte & PTE_PWT) >> 3, (*pte & *pde & PTE_U) >> 2,
	       (*pte & *pde & PTE_W) >> 1);
	return 0;
#endif
}

int mon_cpuinfo(int argc, char **argv, trapframe_t *tf)
{
	cprintf("Number of CPUs detected: %d\n", num_cpus);
	cprintf("Calling CPU's ID: 0x%08x\n", core_id());

	if (argc < 2)
		smp_call_function_self(test_print_info_handler, NULL, 0);
	else
		smp_call_function_single(strtol(argv[1], 0, 10),
		                         test_print_info_handler, NULL, 0);
	return 0;
}

int mon_manager(int argc, char** argv, trapframe_t *tf)
{
	manager();
	panic("should never get here");
	return 0;
}

int mon_nanwan(int argc, char **argv, trapframe_t *tf)
{
	/* Borrowed with love from http://www.geocities.com/SoHo/7373/zoo.htm
	 * (http://www.ascii-art.com/).  Slightly modified to make it 25 lines tall.
	 */
	printk("\n");
	printk("             .-.  .-.\n");
	printk("             |  \\/  |\n");
	printk("            /,   ,_  `'-.\n");
	printk("          .-|\\   /`\\     '. \n");
	printk("        .'  0/   | 0\\  \\_  `\".  \n");
	printk("     .-'  _,/    '--'.'|#''---'\n");
	printk("      `--'  |       /   \\#\n");
	printk("            |      /     \\#\n");
	printk("            \\     ;|\\    .\\#\n");
	printk("            |' ' //  \\   ::\\# \n");
	printk("            \\   /`    \\   ':\\#\n");
	printk("             `\"`       \\..   \\#\n");
	printk("                        \\::.  \\#\n");
	printk("                         \\::   \\#\n");
	printk("                          \\'  .:\\#\n");
	printk("                           \\  :::\\#\n");
	printk("                            \\  '::\\#\n");
	printk("                             \\     \\#\n");
	printk("                              \\:.   \\#\n");
	printk("                               \\::   \\#\n");
	printk("                                \\'   .\\#\n");
	printk("                             jgs \\   ::\\#\n");
	printk("                                  \\      \n");
	return 0;
}

int mon_bin_ls(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	struct dirent dir = {0};
	struct file *bin_dir;
	int retval = 0;

	bin_dir = do_file_open("/bin", 0, 0);
	if (!bin_dir) {
		printk("No /bin directory!\n");
		return 1;
	}
	printk("Files in /bin:\n-------------------------------\n");
	do {
		retval = bin_dir->f_op->readdir(bin_dir, &dir);	
		printk("%s\n", dir.d_name);
	} while (retval == 1);
	kref_put(&bin_dir->f_kref);
	return 0;
}

int mon_bin_run(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	if (argc < 2) {
		printk("Usage: bin_run FILENAME\n");
		return 1;
	}
	struct file *program;
	int retval = 0;
	char buf[6 + MAX_FILENAME_SZ] = "/bin/";	/* /bin/ + max + \0 */
	strncpy(buf + 5, argv[1], MAX_FILENAME_SZ);
	program = do_file_open(buf, 0, 0);
	if (!program) {
		printk("No such program!\n");
		return 1;
	}
	char **p_argv = kmalloc(sizeof(char*) * argc, 0);	/* bin_run's argc */
	for (int i = 0; i < argc - 1; i++)
		p_argv[i] = argv[i + 1];
	p_argv[argc - 1] = 0;
	char *p_envp[] = {"LD_LIBRARY_PATH=/lib", 0};
	struct proc *p = proc_create(program, p_argv, p_envp);
	kfree(p_argv);

	spin_lock(&p->proc_lock);
	__proc_set_state(p, PROC_RUNNABLE_S);
	schedule_proc(p);
	spin_unlock(&p->proc_lock);
	proc_decref(p); /* let go of the reference created in proc_create() */
	kref_put(&program->f_kref);
	/* Should never return from schedule (env_pop in there) also note you may
	 * not get the process you created, in the event there are others floating
	 * around that are runnable */
	schedule();
	return 0;
}

int mon_procinfo(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	if (argc < 2) {
		printk("Usage: procinfo OPTION\n");
		printk("\tidlecores: show idle core map\n");
		printk("\tall: show all active pids\n");
		printk("\trunnable: show proc_runnablelist\n");
		printk("\tresources: show resources wanted/granted for all procs\n");
		printk("\tpid NUM: show a lot of info for proc NUM\n");
		printk("\tunlock: unlock the lock for the ADDR (OMG!!!)\n");
		printk("\tkill NUM: destroy proc NUM\n");
		return 1;
	}
	if (!strcmp(argv[1], "idlecores")) {
		print_idlecoremap();
	} else if (!strcmp(argv[1], "all")) {
		print_allpids();
	} else if (!strcmp(argv[1], "runnable")) {
		dump_proclist(&proc_runnablelist);
	} else if (!strcmp(argv[1], "resources")) {
		print_all_resources();
	} else if (!strcmp(argv[1], "pid")) {
		if (argc != 3) {
			printk("Give me a pid number.\n");
			return 1;
		}
		print_proc_info(strtol(argv[2], 0, 0));
	} else if (!strcmp(argv[1], "unlock")) {
		if (argc != 3) {
			printk("Gimme lock address!  Me want lock address!.\n");
			return 1;
		}
		spinlock_t *lock = (spinlock_t*)strtol(argv[2], 0, 16);
		if (!lock) {
			printk("Null address...\n");
			return 1;
		}
		spin_unlock(lock);
	} else if (!strcmp(argv[1], "kill")) {
		if (argc != 3) {
			printk("Give me a pid number.\n");
			return 1;
		}
		struct proc *p = pid2proc(strtol(argv[2], 0, 0));
		if (!p) {
			printk("No such proc\n");
			return 1;
		}
		proc_destroy(p);
		proc_decref(p);
	} else {
		printk("Bad option\n");
		return 1;
	}
	return 0;
}

int mon_exit(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	return -1;
}

int mon_kfunc(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	#ifndef __i386__
	printk("Only supported on x86 for now.  =(\n");
	return -1;
	#endif

	void (*func)(void *arg, ...);

	if (argc < 2) {
		printk("Usage: kfunc FUNCTION [arg1] [arg2] [etc]\n");
		printk("Arguments must be in hex.  Can take 6 args.\n");
		return 1;
	}
	func = debug_get_fn_addr(argv[1]);
	if (!func) {
		printk("Function not found.\n");
		return 1;
	}
	/* Not elegant, but whatever.  maybe there's a better syntax, or we can do
	 * it with asm magic. */
	switch (argc) {
		case 2: /* have to fake one arg */
			func((void*)0);
			break;
		case 3: /* the real first arg */
			func((void*)strtol(argv[2], 0, 16));
			break;
		case 4:
			func((void*)strtol(argv[2], 0, 16),
			            strtol(argv[3], 0, 16));
			break;
		case 5:
			func((void*)strtol(argv[2], 0, 16),
			            strtol(argv[3], 0, 16),
			            strtol(argv[4], 0, 16));
			break;
		case 6:
			func((void*)strtol(argv[2], 0, 16),
			            strtol(argv[3], 0, 16),
			            strtol(argv[4], 0, 16),
			            strtol(argv[5], 0, 16));
			break;
		case 7:
			func((void*)strtol(argv[2], 0, 16),
			            strtol(argv[3], 0, 16),
			            strtol(argv[4], 0, 16),
			            strtol(argv[5], 0, 16),
			            strtol(argv[6], 0, 16));
			break;
		case 8:
			func((void*)strtol(argv[2], 0, 16),
			            strtol(argv[3], 0, 16),
			            strtol(argv[4], 0, 16),
			            strtol(argv[5], 0, 16),
			            strtol(argv[6], 0, 16),
			            strtol(argv[7], 0, 16));
			break;
		default:
			printk("Bad number of arguments.\n");
			return -1;
	}
	return 0;
}

/* Sending a vcoreid forces an event and an IPI/notification */
int mon_notify(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	struct proc *p;
	uint32_t vcoreid;
	struct event_msg msg = {0};

	if (argc < 3) {
		printk("Usage: notify PID NUM [VCOREID]\n");
		return 1;
	}
	p = pid2proc(strtol(argv[1], 0, 0));
	if (!p) {
		printk("No such proc\n");
		return 1;
	}
	msg.ev_type = strtol(argv[2], 0, 0);
	if (argc == 4) {
		vcoreid = strtol(argv[3], 0, 0);
		post_vcore_event(p, &msg, vcoreid);
		proc_notify(p, vcoreid);
	} else {
		/* o/w, try and do what they want */
		send_kernel_event(p, &msg, 0);
	}
	proc_decref(p);
	return 0;
}

/* Micro-benchmarky Measurements.  This is really fragile code that probably
 * won't work perfectly, esp as the kernel evolves. */
int mon_measure(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	uint64_t begin = 0, diff = 0;
	bool self_ipi_pending = FALSE;
	uint32_t end_refcnt = 0;

	if (argc < 2) {
		printk("Usage: measure OPTION\n");
		printk("\tkill PID : kill proc PID\n");
		printk("\tpreempt PID : preempt proc PID (no delay)\n");
		printk("\tpreempt PID [pcore] : preempt PID's pcore (no delay)\n");
		printk("\tpreempt-warn PID : warn-preempt proc PID (pending)\n");
		printk("\tpreempt-warn PID [pcore] : warn-preempt proc PID's pcore\n");
		printk("\tpreempt-raw PID : raw-preempt proc PID\n");
		printk("\tpreempt-raw PID [pcore] : raw-preempt proc PID's pcore\n");
		return 1;
	}
	if (!strcmp(argv[1], "kill")) {
		if (argc < 3) {
			printk("Give me a pid number.\n");
			return 1;
		}
		struct proc *p = pid2proc(strtol(argv[2], 0, 0));
		if (!p) {
			printk("No such proc\n");
			return 1;
		}
		begin = start_timing();
#ifdef __CONFIG_APPSERVER__
		printk("Warning: this will be inaccurate due to the appserver.\n");
		end_refcnt = kref_refcnt(&p->p_kref) - p->procinfo->num_vcores - 1;
#endif /* __CONFIG_APPSERVER__ */
		proc_destroy(p);
		proc_decref(p);
#ifdef __CONFIG_APPSERVER__
		/* Won't be that accurate, since it's not actually going through the
		 * __proc_free() path. */
		spin_on(kref_refcnt(&p->p_kref) != end_refcnt);	
#else
		/* this is a little ghetto. it's not fully free yet, but we are also
		 * slowing it down by messing with it, esp with the busy waiting on a
		 * hyperthreaded core. */
		spin_on(p->env_cr3);
#endif /* __CONFIG_APPSERVER__ */
		/* No noticeable difference using stop_timing instead of read_tsc() */
		diff = stop_timing(begin);
	} else if (!strcmp(argv[1], "preempt")) {
		if (argc < 3) {
			printk("Give me a pid number.\n");
			return 1;
		}
		struct proc *p = pid2proc(strtol(argv[2], 0, 0));
		if (!p) {
			printk("No such proc\n");
			return 1;
		}
		if (argc == 4) { /* single core being preempted, warned but no delay */
			uint32_t pcoreid = strtol(argv[3], 0, 0);
			begin = start_timing();
			proc_preempt_core(p, pcoreid, 1000000); // 1 sec warning
			/* done when unmapped (right before abandoning) */
			spin_on(p->procinfo->pcoremap[pcoreid].valid);
			diff = stop_timing(begin);
		} else { /* preempt all cores, warned but no delay */
			end_refcnt = kref_refcnt(&p->p_kref) - p->procinfo->num_vcores;
			begin = start_timing();
			proc_preempt_all(p, 1000000);
			/* a little ghetto, implies no one is using p */
			spin_on(kref_refcnt(&p->p_kref) != end_refcnt);
			diff = stop_timing(begin);
		}
		proc_decref(p);
	} else if (!strcmp(argv[1], "preempt-warn")) {
		if (argc < 3) {
			printk("Give me a pid number.\n");
			return 1;
		}
		struct proc *p = pid2proc(strtol(argv[2], 0, 0));
		if (!p) {
			printk("No such proc\n");
			return 1;
		}
		printk("Careful: if this hangs, then the process isn't responding.\n");
		if (argc == 4) { /* single core being preempted-warned */
			uint32_t pcoreid = strtol(argv[3], 0, 0);
			spin_lock(&p->proc_lock);
			uint32_t vcoreid = p->procinfo->pcoremap[pcoreid].vcoreid;
			if (!p->procinfo->pcoremap[pcoreid].valid) {
				printk("Pick a mapped pcore\n");
				spin_unlock(&p->proc_lock);
				return 1;
			}
			begin = start_timing();
			__proc_preempt_warn(p, vcoreid, 1000000); // 1 sec
			spin_unlock(&p->proc_lock);
			/* done when unmapped (right before abandoning) */
			spin_on(p->procinfo->pcoremap[pcoreid].valid);
			diff = stop_timing(begin);
		} else { /* preempt-warn all cores */
			printk("Warning, this won't work if they can't yield their "
			       "last vcore, will stop at 1!\n");
			spin_lock(&p->proc_lock);
			begin = start_timing();
			__proc_preempt_warnall(p, 1000000);
			spin_unlock(&p->proc_lock);
			/* target cores do the unmapping / changing of the num_vcores */
			spin_on(p->procinfo->num_vcores > 1);
			diff = stop_timing(begin);
		}
		proc_decref(p);
	} else if (!strcmp(argv[1], "preempt-raw")) {
		if (argc < 3) {
			printk("Give me a pid number.\n");
			return 1;
		}
		struct proc *p = pid2proc(strtol(argv[2], 0, 0));
		if (!p) {
			printk("No such proc\n");
			return 1;
		}
		if (argc == 4) { /* single core preempted, no warning or waiting */
			uint32_t pcoreid = strtol(argv[3], 0, 0);
			spin_lock(&p->proc_lock);
			if (!p->procinfo->pcoremap[pcoreid].valid) {
				printk("Pick a mapped pcore\n");
				spin_unlock(&p->proc_lock);
				return 1;
			}
			begin = start_timing();
			self_ipi_pending = __proc_preempt_core(p, pcoreid);
			spin_unlock(&p->proc_lock);
			__proc_kmsg_pending(p, self_ipi_pending);
			/* done when unmapped (right before abandoning) */
			spin_on(p->procinfo->pcoremap[pcoreid].valid);
			diff = stop_timing(begin);
			/* TODO: (RMS), if num_vcores == 0, RUNNABLE_M, schedule */
		} else { /* preempt all cores, no warning or waiting */
			spin_lock(&p->proc_lock);
			end_refcnt = kref_refcnt(&p->p_kref) - p->procinfo->num_vcores;
			begin = start_timing();
			self_ipi_pending = __proc_preempt_all(p);
			/* TODO: (RMS), RUNNABLE_M, schedule */
			spin_unlock(&p->proc_lock);
			__proc_kmsg_pending(p, self_ipi_pending);
			/* a little ghetto, implies no one else is using p */
			spin_on(kref_refcnt(&p->p_kref) != end_refcnt);
			diff = stop_timing(begin);
		}
		proc_decref(p);
	} else {
		printk("Bad option\n");
		return 1;
	}
	printk("[Tired Giraffe Accent] Took %llu usec (%llu nsec) to finish.\n",
	       diff * 1000000 / system_timing.tsc_freq,
	       diff * 1000000000 / system_timing.tsc_freq);
	return 0;
}

int mon_trace(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	if (argc < 2) {
		printk("Usage: trace OPTION\n");
		printk("\tsyscall start [silent] [pid]: starts tracing\n");
		printk("\tsyscall stop: stops tracing, prints if it was silent\n");
		return 1;
	}
	if (!strcmp(argv[1], "syscall")) {
		if (argc < 3) {
			printk("Need a start or stop.\n");
			return 1;
		}
		if (!strcmp(argv[2], "start")) {
			bool all = TRUE;
			bool silent = FALSE;
			struct proc *p = NULL;
			if (argc >= 4) {
				silent = (bool)strtol(argv[3], 0, 0);
			}
			if (argc >= 5) {
				all = FALSE;
				p = pid2proc(strtol(argv[4], 0, 0));
				if (!p) {
					printk("No such process\n");
					return 1;
				}
			}
			systrace_start(silent);
			if (systrace_reg(all, p))
				printk("No room to trace more processes\n");
		} else if (!strcmp(argv[2], "stop")) {
			/* Stop and print for all processes */
			systrace_stop();
			systrace_print(TRUE, 0);
			systrace_clear_buffer();
		}
	} else if (!strcmp(argv[1], "opt2")) {
		if (argc != 3) {
			printk("ERRRRRRRRRR.\n");
			return 1;
		}
		print_proc_info(strtol(argv[2], 0, 0));
	} else {
		printk("Bad option\n");
		return 1;
	}
	return 0;
}

int mon_monitor(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	if (argc < 2) {
		printk("Usage: monitor COREID\n");
		return 1;
	}
	uint32_t core = strtol(argv[1], 0, 0);
	if (core >= num_cpus) {
		printk("No such core!  Maybe it's in another cell...\n");
		return 1;
	}
	void run_mon(struct trapframe *tf, uint32_t srcid, void *a0, void *a1,
	             void *a2)
	{
		monitor(0); // TODO consider passing the tf
	}
	send_kernel_message(core, run_mon, 0, 0, 0, KMSG_ROUTINE);
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int runcmd(char *NTS real_buf, trapframe_t *tf) {
	char * buf = NTEXPAND(real_buf);
	int argc;
	char *NTS argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		//This will get fucked at runtime..... in the ASS
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void monitor(struct trapframe *tf)
{
	#define MON_CMD_LENGTH 256
	char buf[MON_CMD_LENGTH];
	int cnt;

	/* they are always disabled, since we have this irqsave lock */
	if (irq_is_enabled())
		printk("Entering Nanwan's Dungeon on Core %d (Ints on):\n", core_id());
	else
		printk("Entering Nanwan's Dungeon on Core %d (Ints off):\n", core_id());
	printk("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		cnt = readline(buf, MON_CMD_LENGTH, "ROS(Core %d)> ", core_id());
		if (cnt > 0) {
			buf[cnt] = 0;
			if (runcmd(buf, tf) < 0)
				break;
		}
	}
}

int mon_fs(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	/* this assumes one mounted FS at the NS root */
	struct super_block *sb;
	struct file *file;
	struct inode *inode;
	struct dentry *dentry;
	if (argc < 2) {
		printk("Usage: fs OPTION\n");
		printk("\topen: show all open files\n");
		printk("\tinodes: show all inodes\n");
		printk("\tdentries [lru|prune]: show all dentries, opt LRU/prune\n");
		printk("\tls DIR: print the dir tree starting with DIR\n");
		printk("\tpid: proc PID's fs crap placeholder\n");
		return 1;
	}
	if (!strcmp(argv[1], "open")) {
		printk("Open Files:\n----------------------------\n");
		TAILQ_FOREACH(sb, &super_blocks, s_list) {
			printk("Superblock for %s\n", sb->s_name);
			TAILQ_FOREACH(file, &sb->s_files, f_list)
				printk("File: %08p, %s, Refs: %d, Drefs: %d, Irefs: %d\n", file,
				       file_name(file), kref_refcnt(&file->f_kref),
				       kref_refcnt(&file->f_dentry->d_kref),
				       kref_refcnt(&file->f_dentry->d_inode->i_kref));
		}
	} else if (!strcmp(argv[1], "inodes")) {
		printk("Mounted FS Inodes:\n----------------------------\n");
		TAILQ_FOREACH(sb, &super_blocks, s_list) {
			printk("Superblock for %s\n", sb->s_name);
			TAILQ_FOREACH(inode, &sb->s_inodes, i_sb_list) {
				printk("Inode: %08p, Refs: %d, Nlinks: %d, Size(B): %d\n",
				       inode, kref_refcnt(&inode->i_kref), inode->i_nlink,
				       inode->i_size);
				TAILQ_FOREACH(dentry, &inode->i_dentry, d_alias)
					printk("\t%s: Dentry: %08p, Refs: %d\n",
					       dentry->d_name.name, dentry,
					       kref_refcnt(&dentry->d_kref));
			}
		}
	} else if (!strcmp(argv[1], "dentries")) {
		printk("Dentry Cache:\n----------------------------\n");
		TAILQ_FOREACH(sb, &super_blocks, s_list) {
			printk("Superblock for %s\n", sb->s_name);
			printk("DENTRY     FLAGS      REFCNT NAME\n");
			printk("--------------------------------\n");
			/* Hash helper */
			void print_dcache_entry(void *item)
			{
				struct dentry *d_i = (struct dentry*)item;
				printk("%08p %08p %02d     %s\n", d_i, d_i->d_flags,
				       kref_refcnt(&d_i->d_kref), d_i->d_name.name);
			}
			hash_for_each(sb->s_dcache, print_dcache_entry);
		}
		if (argc < 3)
			return 0;
		if (!strcmp(argv[2], "lru")) {
			printk("LRU lists:\n");
			TAILQ_FOREACH(sb, &super_blocks, s_list) {
				printk("Superblock for %s\n", sb->s_name);
				TAILQ_FOREACH(dentry, &sb->s_lru_d, d_lru)
					printk("Dentry: %08p, Name: %s\n", dentry,
					       dentry->d_name.name);
			}
		} else if (!strcmp(argv[2], "prune")) {
			printk("Pruning unused dentries\n");
			TAILQ_FOREACH(sb, &super_blocks, s_list)
				dcache_prune(sb, FALSE);
		}
	} else if (!strcmp(argv[1], "ls")) {
		if (argc != 3) {
			printk("Give me a dir.\n");
			return 1;
		}
		if (argv[2][0] != '/') {
			printk("Dear fellow giraffe lover, Use absolute paths.\n");
			return 1;
		}
		ls_dash_r(argv[2]);
		/* whatever.  placeholder. */
	} else if (!strcmp(argv[1], "pid")) {
		if (argc != 3) {
			printk("Give me a pid number.\n");
			return 1;
		}
		/* whatever.  placeholder. */
	} else {
		printk("Bad option\n");
		return 1;
	}
	return 0;
}

int mon_bb(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	char *l_argv[3] = {"", "busybox", "ash"};
	return mon_bin_run(3, l_argv, tf);
}

int mon_alarm(int argc, char **argv, struct trapframe *tf)
{
	if (argc < 2) {
		printk("Usage: alarm OPTION\n");
		printk("\tpcpu: print full alarm tchains from every core\n");
		return 1;
	}
	if (!strcmp(argv[1], "pcpu")) {
		print_pcpu_chains();
	} else {
		printk("Bad option\n");
		return 1;
	}
	return 0;
}

/* This is ghetto: assumes a VCPD 10 element BCQ.  This is a pain in the ass to
 * do generically, since size of a struct doesn't tell you its alignment.
 * (picture a struct with just chars vs one that requires padding due to longer
 * members).  This just works for event BCQs. */
int mon_bcq(int argc, char **argv, struct trapframe *tf)
{
	if (argc < 2) {
		printk("Usage: bcq OPTION [KVA_OF_BCQ] \n");
		printk("\tstatus KVA_OF_BCQ: prints the status of the bcq\n");
		return 1;
	}
	if (!strcmp(argv[1], "status")) {
		if (argc != 3) {
			printk("Give me a bcq.\n");
			return 1;
		}
		struct event_msg_bcq *bcq =
		                     (struct event_msg_bcq*)strtol(argv[2], 0, 16);
		printk("Event BCQ: %08p\n", bcq);
		printk("\tprod_idx: %08p\n", bcq->hdr.prod_idx);
		printk("\tcons_pub_idx: %08p\n", bcq->hdr.cons_pub_idx);
		printk("\tcons_pvt_idx: %08p\n", bcq->hdr.cons_pvt_idx);
		for (int i = 0; i < NR_BCQ_EVENTS; i++) {
			printk("Element %d, rdy_for_cons: %02p\n", i,
			       bcq->wraps[i].rdy_for_cons);
		}
	} else {
		printk("Bad option\n");
		return 1;
	}
	return 0;
}
