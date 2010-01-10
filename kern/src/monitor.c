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
#include <kfs.h>
#include <manager.h>
#include <schedule.h>
#include <resource.h>

#include <ros/memlayout.h>

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
	{ "showmapping", "Shows VA->PA mappings between two virtual addresses (parameters)", mon_showmapping},
	{ "setmapperm", "Sets permissions on a VA->PA mapping", mon_setmapperm},
	{ "cpuinfo", "Prints CPU diagnostics", mon_cpuinfo},
	{ "ps", "Prints process list", mon_ps},
	{ "nanwan", "Meet Nanwan!!", mon_nanwan},
	{ "kfs_ls", "List files in KFS", mon_kfs_ls},
	{ "kfs_run", "Create and run a program from KFS", mon_kfs_run},
	{ "manager", "Run the manager", mon_manager},
	{ "procinfo", "Show information about processes", mon_procinfo},
	{ "exit", "Leave the monitor", mon_exit},
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
	cprintf("[Irish Accent]: She's goin' down, Cap'n!\n");
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

#ifdef __i386__
	if (argc < 2)
		smp_call_function_self(test_print_info_handler, NULL, 0);
	else
		smp_call_function_single(strtol(argv[1], 0, 16),
		                         test_print_info_handler, NULL, 0);
#endif
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

int mon_kfs_ls(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	printk("Files in KFS:\n-------------------------------\n");
	for (int i = 0; i < MAX_KFS_FILES; i++)
		if (kfs[i].name[0])
			printk("%s\n", kfs[i].name);
	return 0;
}

int mon_kfs_run(int argc, char *NTS *NT COUNT(argc) argv, trapframe_t *tf)
{
	if (argc != 2) {
		printk("Usage: kfs_run FILENAME\n");
		return 1;
	}
	int kfs_inode = kfs_lookup_path(argv[1]);
	if (kfs_inode < 0) {
		printk("Bad filename!\n");
		return 1;
	}
	struct proc *p = kfs_proc_create(kfs_inode);
	// go from PROC_CREATED->PROC_RUNNABLE_S
	spin_lock_irqsave(&p->proc_lock); // might not be necessary for a mon function
	__proc_set_state(p, PROC_RUNNABLE_S);
	schedule_proc(p);
	spin_unlock_irqsave(&p->proc_lock);
	proc_decref(p, 1); // let go of the reference created in proc_create()
	// Should never return from schedule (env_pop in there)
	// also note you may not get the process you created, in the event there
	// are others floating around that are runnable
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
		printk("\tunlock NUM: unlock the lock for proc NUM (OMG!!!)\n");
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
			printk("Give me a pid number.\n");
			return 1;
		}
		struct proc *p = pid2proc(strtol(argv[2], 0, 0));
		if (!p) {
			printk("No such proc\n");
			return 1;
		}
		spin_unlock_irqsave(&p->proc_lock);
		proc_decref(p, 1);
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
		proc_decref(p, 1);
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

void monitor(trapframe_t *tf) {
	char *buf;

	printk("Welcome to the ROS kernel monitor on core %d!\n", core_id());
	printk("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
