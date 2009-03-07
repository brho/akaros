/* See COPYRIGHT for copyright information. */

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/multiboot.h>
#include <inc/stab.h>
#include <inc/x86.h>

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
	//ENV_CREATE(user_faultread);
	//ENV_CREATE(user_faultreadkernel);
	//ENV_CREATE(user_faultwrite);
	//ENV_CREATE(user_faultwritekernel);
	//ENV_CREATE(user_breakpoint);
	//ENV_CREATE(user_badsegment);
	//ENV_CREATE(user_divzero);
	ENV_CREATE(user_hello);
	//ENV_CREATE(user_buggyhello);
	//ENV_CREATE(user_evilhello);
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
	uint32_t eax, ebx, ecx, edx;
	uint32_t model, family;
	uint64_t msr_val;
	char vendor_id[13];

	asm volatile ("cpuid;"
                  "movl    %%ebx, (%2);"
                  "movl    %%edx, 4(%2);"
                  "movl    %%ecx, 8(%2);"
	              : "=a"(eax) 
				  : "a"(0), "D"(vendor_id)
	              : "%ebx", "%ecx", "%edx");

	vendor_id[12] = '\0';
	cprintf("Vendor ID: %s\n", vendor_id);
	cprintf("Largest Standard Function Number Supported: %d\n", eax);
	cpuid(0x80000000, &eax, 0, 0, 0);
	cprintf("Largest Extended Function Number Supported: 0x%08x\n", eax);
	cpuid(1, &eax, &ebx, &ecx, &edx);
	family = ((eax & 0x0FF00000) >> 20) + ((eax & 0x00000F00) >> 8);
	model = ((eax & 0x000F0000) >> 12) + ((eax & 0x000000F0) >> 4);
	cprintf("Family: %d\n", family);
	cprintf("Model: %d\n", model);
	cprintf("Stepping: %d\n", eax & 0x0000000F);
	// eventually can fill this out with SDM Vol3B App B info, or 
	// better yet with stepping info.  
	switch ( family << 8 | model ) {
		case(0x060f):
			cprintf("Processor: Core 2 Duo or Similar\n");
			break;
		default:
			cprintf("Unknown or non-Intel CPU\n");
	}
	if (edx & 0x00000010)
		cprintf("Model Specific Registers supported\n");
	else
		panic("MSRs not supported!");
	if (edx & 0x00000100)
		cprintf("Local APIC Detected\n");
	else
		panic("Local APIC Not Detected!");
	
	cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
	cprintf("Physical Address Bits: %d\n", eax & 0x000000FF);


	/*
	msr_val = read_msr(IA32_APIC_BASE);
	msr_val & ~MSR_APIC_ENABLE;
	write_msr(IA32_APIC_BASE, msr_val);
	if (edx & 0x00000100)
		cprintf("Local APIC Detected\n");
	else
		panic("Local APIC Not Detected!");
		*/
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


