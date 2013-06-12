/* Copyright (c) 2009-13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <arch/mmu.h>
#include <arch/trap.h>
#include <ros/memlayout.h>

# Shift Right Logical 
#define SRL(val, shamt)		(((val) >> (shamt)) & ~(-1 << (32 - (shamt))))

.set CODE_SEL,0x8		# index of code seg within mygdt
.set DATA_SEL,0x10		# index of data seg within mygdt

#define MULTIBOOT_PAGE_ALIGN  (1<<0)
#define MULTIBOOT_MEMORY_INFO (1<<1)
#define MULTIBOOT_HEADER_MAGIC (0x1BADB002)
#define MULTIBOOT_HEADER_FLAGS (MULTIBOOT_MEMORY_INFO | MULTIBOOT_PAGE_ALIGN)
#define CHECKSUM (-(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS))

# The kernel bootstrap (this code) is linked and loaded at physical address
# 0x00100000 (1MB), which is the start of extended memory.  (See kernel.ld)

# Flagging boottext to be text.  Check out:
# http://sourceware.org/binutils/docs/as/Section.html
.section .boottext, "awx"

.code32
.align 4
multiboot_header:
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long CHECKSUM

.globl		_start
_start:
	movw	$0x1234,0x472			# warm boot
	# Reload all segment registers (including CS!) with flag segment selectors
	# from our boot GDT.
	lgdt	mygdtdesc
	movl	$DATA_SEL, %eax
	movw	%ax,%ds
	movw	%ax,%es
	movw	%ax,%ss
	ljmp	$CODE_SEL,$newcs		# reload CS by jumping
newcs:
	# build page table.  need a mapping for current code at 0x00100000 and a
	# basic kernbase mapping.  we're using the 32 bit second PT (aka, pg_dir),
	# which covers 4MB per entry
	movl	$boot_pdt, %edx
	# identity map the first jumbo PTE from 0x0 -> 0x0
	movl	$(PTE_P | PTE_W | PTE_PS), (%edx)
	# map KERNBASE -> 0 for 200 MB
	movl	$50, %ecx
	# init loop, eax at paddr 0, and edx is advanced by KERNBASE mapping slots
	# (with 4 bytes per PTE).
	# XXX addl	$((KERNBASE >> PTSHIFT) << 2), %edx
	movl	$(PTE_P | PTE_W | PTE_PS), %eax
loop:
	movl	%eax, (%edx)
	addl	$PTSIZE, %eax
	addl	$4, %edx
	decl	%ecx
	jnz		loop

# hack to compile / link
spin2:
	jmp spin2

	# load cr3 and turn on paging.  note we assume PSE support.  if we didn't
	# have it, then our jumbo page mappings are going to fail.
	movl	$boot_pdt, %eax
	movl	%eax, %cr3
	movl	%cr4, %eax
	orl		$(CR4_PSE | CR4_PGE), %eax
	movl	%eax, %cr4
	movl	%cr0, %eax
	orl		$(CR0_PE | CR0_PG | CR0_AM | CR0_WP | CR0_NE | CR0_MP), %eax  
	andl	$(~(CR0_TS | CR0_EM | CR0_CD | CR0_NW)), %eax  
	movl	%eax, %cr0
	# paging is on, and our code is still running at 0x00100000 do some
	# miscellaneous OS setup.  the coreid stuff is so we can call core_id()
	# before smp_boot.  this is the only arch-dependent code called before then.
.code64 # temp hack for linkage.  also, this symbol is too far at link time
	movabs 	$(os_coreid_lookup), %rax
	movl	$0x0, (%rax)
	movabs 	$(hw_coreid_lookup), %rax
	movl	$0x0, (%rax)
	# Clear the frame pointer register (EBP)
	# so that once we get into debugging C code,
	# stack backtraces will be terminated properly.
	movl	$0x0,%ebp
 	movabs	$(bootstacktop),%rsp
	# Save multiboot info
	push 	%rbx
	movabs 	$(num_cpus), %rax
	movl	$0x1, (%rax)

	movabs	$(kernel_init), %rax
	call	*%rax
	# Should never get here, but in case we do, just spin.
spin:	jmp	spin

.section .bootdata, "aw"
	.p2align	2		# force 4 byte alignment
mygdt:
	SEG_NULL			# null seg
	SEG(STA_X|STA_R, 0, 0xffffffff)	# code seg
	SEG(STA_W, 0, 0xffffffff)	# data seg
mygdtdesc:
	.word	0x17		# sizeof(mygdt) - 1
	.long	mygdt		# address mygdt
# boot page directory.  going to use jumbo page entries
	.align PGSIZE
boot_pdt:
	.space  PGSIZE


# From here down is linked for KERNBASE

###################################################################	
# See <inc/memlayout.h> for a complete description of these two symbols.
###################################################################
.data
	.globl	vpt
	.quad	vpt
	.set	vpt, VPT
	.globl	vpd
	.quad	vpd
	.set	vpd, (VPT + SRL(VPT, 10))

###################################################################
# boot stack
###################################################################
	.p2align	PGSHIFT		# force page alignment
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:

