// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/stab.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/trap.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line

typedef struct command {
	const char *NTS name;
	const char *NTS desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char *NTS *NT COUNT(argc) argv, struct Trapframe* tf);
} command_t;

static command_t commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Dump a backtrace", mon_backtrace },
	{ "reboot", "Take a ride to the South Bay", mon_reboot },
	{ "showmapping", "Shows VA->PA mappings between two virtual addresses (parameters)", mon_showmapping},
	{ "setmapperm", "Sets permissions on a VA->PA mapping", mon_setmapperm},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char (SNT _start)[], (SNT etext)[], (SNT edata)[], (SNT end)[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, (uint32_t)(_start - KERNBASE));
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, (uint32_t)(etext - KERNBASE));
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, (uint32_t)(edata - KERNBASE));
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, (uint32_t)(end - KERNBASE));
	cprintf("Kernel executable memory footprint: %dKB\n",
		(uint32_t)(end-_start+1023)/1024);
	return 0;
}

static char* function_of(uint32_t address) 
{
	extern stab_t stab[], estab[];
	extern char stabstr[];
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

int mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t* ebp, eip;
	struct Eipdebuginfo debuginfo;
	char buf[256];
	int j, i = 1;
	ebp = (uint32_t*)read_ebp();	
	// this is the retaddr for what called backtrace
	eip = *(ebp + 1);
	// jump back a frame (out of mon_backtrace)
	ebp = (uint32_t*)(*ebp);
	cprintf("Stack Backtrace:\n");
	// on each iteration, ebp holds the stack frame and eip is a retaddr in that func
	while (ebp != 0) {
		debuginfo_eip(eip, &debuginfo);
		memset(buf, 0, 256);
		strncpy(buf, debuginfo.eip_fn_name, MIN(debuginfo.eip_fn_namelen, 256));
		buf[MIN(debuginfo.eip_fn_namelen, 255)] = 0;
		cprintf("#%02d [<%x>] in %s+%x(%p) from %s:%d\n", i++,  eip, buf, 
		        debuginfo.eip_fn_addr - (uint32_t)_start, debuginfo.eip_fn_addr, 
		        debuginfo.eip_file, debuginfo.eip_line);
		cprintf("    ebp: %x   Args:", ebp);
		for (j = 0; j < MIN(debuginfo.eip_fn_narg, 5); j++)
			cprintf(" %08x", *(ebp + 2 + j));
		cprintf("\n");
		eip = *(ebp + 1);
		ebp = (uint32_t*)(*ebp);
	}
	return 0;
}

int mon_reboot(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("[Irish Accent]: She's goin' down, Cap'n!\n");
	outb(0x92, 0x3);
	cprintf("Should have rebooted.  Doesn't work yet in KVM...\n");
	return 0;
}

int mon_showmapping(int argc, char **argv, struct Trapframe *tf)
{
	if (argc < 2) {
		cprintf("Shows virtual -> physical mappings for a virtual address range.\n");
		cprintf("Usage: showmappings START_ADDR [END_ADDR]\n");
		return 1;
	}
	pde_t* pgdir = (pde_t*)vpd;
	pte_t* pte;
	struct Page* page;
	uintptr_t start, i;
	size_t size;
	start = ROUNDDOWN(strtol(argv[1], 0, 16), PGSIZE);
	size = (argc == 2) ? 1 : strtol(argv[2], 0, 16) - start;
	if (size/PGSIZE > 512) {
		cprintf("Not going to do this for more than 512 items\n");
		return 1;
	}
	cprintf("   Virtual    Physical  Ps Dr Ac CD WT U W\n");
	cprintf("------------------------------------------\n");
	for(i = 0; i < size; i += PGSIZE, start += PGSIZE) {
		page = page_lookup(pgdir, (void*)start, &pte);
		cprintf("%08p  ", start);
		if (page) 
			cprintf("%08p  %1d  %1d  %1d  %1d  %1d  %1d %1d\n", page2pa(page), 
			       (*pte & PTE_PS) >> 7, (*pte & PTE_D) >> 6, (*pte & PTE_A) >> 5,
			       (*pte & PTE_PCD) >> 4, (*pte & PTE_PWT) >> 3, 
			       (*pte & PTE_U) >> 2, (*pte & PTE_W) >> 1);
		else
			cprintf("%08p\n", 0);
	}
	return 0;
}

int mon_setmapperm(int argc, char **argv, struct Trapframe *tf)
{
	if (argc < 2) {
		cprintf("Sets VIRT_ADDR's mapping's permissions to PERMS (in hex)\n");
		cprintf("Usage: setmapperm VIRT_ADDR PERMS\n");
		return 1;
	}
	pde_t* pgdir = (pde_t*)vpd;
	pte_t* pte;
	struct Page* page;
	uintptr_t va;
	va = ROUNDDOWN(strtol(argv[1], 0, 16), PGSIZE);
	page = page_lookup(pgdir, (void*)va, &pte);
	if (!page) {
		cprintf("No such mapping\n");
		return 1;
	}
	cprintf("   Virtual    Physical  Ps Dr Ac CD WT U W\n");
	cprintf("------------------------------------------\n");
	cprintf("%08p  %08p  %1d  %1d  %1d  %1d  %1d  %1d %1d\n", va, page2pa(page), 
	       (*pte & PTE_PS) >> 7, (*pte & PTE_D) >> 6, (*pte & PTE_A) >> 5, 
	       (*pte & PTE_PCD) >> 4, (*pte & PTE_PWT) >> 3, (*pte & PTE_U) >> 2, 
	       (*pte & PTE_W) >> 1);
	*pte = PTE_ADDR(*pte) | (*pte & PTE_PS) |
	       (PGOFF(strtol(argv[2], 0, 16)) & ~PTE_PS ) | PTE_P;
	cprintf("%08p  %08p  %1d  %1d  %1d  %1d  %1d  %1d %1d\n", va, page2pa(page), 
	       (*pte & PTE_PS) >> 7, (*pte & PTE_D) >> 6, (*pte & PTE_A) >> 5, 
	       (*pte & PTE_PCD) >> 4, (*pte & PTE_PWT) >> 3, (*pte & PTE_U) >> 2, 
	       (*pte & PTE_W) >> 1);
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int runcmd(char *COUNT(CMDBUF_SIZE) real_buf, struct Trapframe* tf) {
	char *BND(real_buf, real_buf+CMDBUF_SIZE) buf = real_buf;
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
		argv[argc++] = (char *NTS) TC(buf);
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

void monitor(struct Trapframe* tf) {
	char *buf;

	cprintf("Welcome to the ROS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
