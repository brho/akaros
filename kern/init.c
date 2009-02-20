/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/multiboot.h>
#include <inc/stab.h>

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>

void print_cpuinfo(void);

void kernel_init(multiboot_info_t *mboot_info)
{
	extern char (BND(__this, end) edata)[], (SNT end)[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	print_cpuinfo();

	// Lab 2 memory management initialization functions
	i386_detect_memory();
	i386_vm_init();
	page_init();
	page_check();

	// Lab 3 user environment initialization functions
	env_init();
	idt_init();

	// Temporary test code specific to LAB 3
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE2(TEST, TESTSIZE);
#else
	// Touch all you want.
	ENV_CREATE(user_softint);
	ENV_CREATE(user_badsegment);
	ENV_CREATE(user_divzero);
	ENV_CREATE(user_hello);
#endif // TEST*

	// We only have one user environment for now, so just run it.
	env_run(&envs[0]);
}


/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
static const char *NTS panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void _panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void _warn(const char *file, int line, const char *fmt,...) 
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}

void print_cpuinfo(void) {
	int func_num;
	char vendor_id[13];

	asm volatile ("subl    %0, %0;
                   cpuid;
                   movl    %%ebx, (%1);
                   movl    %%edx, 4(%1);
                   movl    %%ecx, 8(%1)"
	              : "=a"(func_num) 
				  : "D"(vendor_id)
	              : "%ebx", "%ecx", "%edx");

	vendor_id[12] = '\0';
	cprintf("Largest Standard Function Number Supported: %d\n", func_num);
	cprintf("Vendor ID: %s\n", vendor_id);
}


	/* Backup of old shit that i hoard for no reason
	 *
	 * all of this was in kernel_init
	cprintf("6828 decimal is %o octal!\n", 6828);
	cprintf("Symtab section should begin at: 0x%x \n", stab);

	// double check
	mboot_info = (multiboot_info_t*)(0xc0000000 + (char*)mboot_info);
	cprintf("Mboot info address: %p\n", mboot_info);
	elf_section_header_table_t *elf_head= &(mboot_info->u.elf_sec);
	cprintf("elf sec info address: %p\n", elf_head);
    cprintf ("elf_sec: num = %u, size = 0x%x, addr = 0x%x, shndx = 0x%x\n",
            elf_head->num, elf_head->size,
            elf_head->addr, elf_head->shndx);
	
	struct Secthdr *elf_sym = (struct Secthdr*)(0xc0000000 + elf_head->addr + elf_head->size * 3);

	cprintf("Symtab multiboot struct address: %p\n", elf_sym);
	cprintf("Symtab multiboot address: %p\n", elf_sym->sh_addr);

	// this walks a symtable, but we don't have one...
	Elf32_Sym* symtab = (Elf32_Sym*)stab;
	Elf32_Sym* oldsymtab = symtab;
	for (; symtab < oldsymtab + 10 ; symtab++) {
		cprintf("Symbol name index = 0x%x\n", symtab->st_name);
		//cprintf("Symbol name = %s\n", stabstr + symtab->st_name);
		cprintf("Symbol vale = 0x%x\n", symtab->st_value);
	}
	*/
	/*
	extern stab_t stab[], estab[];
	extern char stabstr[];
	stab_t* symtab;
	// Spits out the stabs for functions
	for (symtab = stab; symtab < estab; symtab++) {
		// gives us only functions.  not really needed if we scan by address
		if (symtab->n_type != 36)
			continue;
		cprintf("Symbol name = %s\n", stabstr + symtab->n_strx);
		cprintf("Symbol type = %d\n", symtab->n_type);
		cprintf("Symbol value = 0x%x\n", symtab->n_value);
		cprintf("\n");
	}
	*/


