/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <arch/mmu.h>
#include <arch/trap.h>
#include <arch/x86.h>
#include <ros/memlayout.h>

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

/* Helper: creates count mappings in the PML3 for 1GB jumbo pages for the given
 * vaddr to paddr range in physical memory.  Then it puts that PML3's addr in
 * the PML4's appropriate slot.  A few notes:
 * 	- PML3 is responsible for the 9 bits from 30-38, hence the >> 30 and mask
 * 	- PML4 is responsible for the 9 bits from 47-39, hence the >> 39 and mask
 * 	- We use the jumbo PTE_PS flag only on PML3 - can't do it for PML4.
 * 	- PTEs are 8 bytes each, hence the scale = 8 in the indirect addressing
 * 	- The top half of all of PML4's PTEs are set to 0.  This includes the top 20
 * 	bits of the physical address of the page tables - which are 0 in our case.
 * 	- The paddr for the PML3 PTEs is split across two 32-byte halves of the PTE.
 * 	We drop off the lower 30 bits, since we're dealing with 1GB pages.  The 2
 * 	LSBs go at the top of the first half of the PTE, and the remaining 30 are
 * 	the lower 30 of the top half.
 * 	- In general, I use eax as the offset in a PML (the entry is determined by
 * 	the vaddr), edx for the entry I'm writing, and ecx for the value I'm
 * 	writing.  For PML3's mappings, esi tracks which paddr we are on.  Finally,
 * 	I'm using edi for loop control. */
#define MAP_GB_PAGES(pml3, vaddr, paddr, count)                                \
	movl	$(pml3), %edx;                                                     \
	movl	$(((vaddr) >> 30) & 0x1ff), %eax;                                  \
	movl	$((paddr) >> 30), %esi;                                            \
	movl	$(count), %edi;                                                    \
1:;                                                                            \
	movl	%esi, %ecx;                                                        \
	shll	$30, %ecx;                       /* lower part of PTE ADDR */      \
	orl		$(PTE_P | PTE_W | PTE_PS), %ecx;                                   \
	movl	%ecx, (%edx, %eax, 8);                                             \
	movl	%esi, %ecx;                                                        \
	shrl	$2, %ecx;                        /* upper part of PTE ADDR */      \
	movl	%ecx, 4(%edx, %eax, 8);                                            \
	/* prep for next loop */;                                                  \
	incl	%eax;                                                              \
	incl	%esi;                                                              \
	/* could test eax against count, if we're short on regs */;                \
	decl	%edi;                                                              \
	jnz		1b;                                                                \
	/* now insert the PML3 into pml4 */;                                       \
	movl	$(((vaddr) >> 39) & 0x1ff), %eax;                                  \
	movl	$boot_pml4, %edx;                                                  \
	movl	$(pml3), %ecx;                                                     \
	orl		$(PTE_P | PTE_W), %ecx;                                            \
	movl	%ecx, (%edx, %eax, 8);                                             \
	movl	$0x0, 4(%edx, %eax, 8)

.globl		_start
_start:
	movw	$0x1234,0x472			# warm boot
	# build page table.  need mappings for
	# 	- current code/data at 0x00100000 -> 0x00100000
	#	- kernel load location: 0xffffffffc0000000 -> 0x0000000000000000
	#	- kernbase: 0xffff80000000 -> 0x0000000000000000
	# we'll need one table for the PML4, and three PML3 (PDPE)'s.  1GB will
	# suffice for lo and hi (til we do the VPT and LAPIC mappings).  For
	# kernbase, we'll do all 512 PML3 entries (covers 512GB)
	MAP_GB_PAGES(boot_pml3_lo,       0x0000000000000000, 0x0, 1)
	MAP_GB_PAGES(boot_pml3_hi,       0xffffffffc0000000, 0x0, 1)
	MAP_GB_PAGES(boot_pml3_kernbase, 0xffff800000000000, 0x0, 512)
	# load cr3 - note that in long mode, cr3 is 64 bits wide.  our boot pml4 is
	# in lower memory, so it'll be fine if the HW 0 extends.
	movl	$boot_pml4, %eax
	movl	%eax, %cr3
	# turn on paging option in cr4.  note we assume PSE support.  if we didn't
	# have it, then our jumbo page mappings are going to fail.  we also want
	# global pages (for performance).  PAE is the basics needed for long paging
	movl	%cr4, %eax
	orl		$(CR4_PSE | CR4_PGE | CR4_PAE), %eax
	movl	%eax, %cr4
	# Turn on the IA32E enabled bit.
	# rd/wrmsr use ecx for the addr, and eax as the in/out register.
	movl	$IA32_EFER_MSR, %ecx
	rdmsr
	orl		$IA32_EFER_IA32E_EN, %eax
	wrmsr
	# Setup cr0.  PE and PG are critical for now.  The others are similar to
	# what we want in general (-AM with 64 bit, it's useless).
	movl	%cr0, %eax
	orl		$(CR0_PE | CR0_PG | CR0_WP | CR0_NE | CR0_MP), %eax  
	andl	$(~(CR0_AM | CR0_TS | CR0_EM | CR0_CD | CR0_NW)), %eax  
	movl	%eax, %cr0
	# load the 64bit GDT and jump to long mode
	lgdt	gdt64desc
	ljmp	$0x08, $long_mode
.code64
long_mode:
	# zero the data segments.  Not sure if this is legit or not.
	xor 	%rax, %rax
	mov 	%ax, %ds
	mov 	%ax, %es
	mov 	%ax, %ss
	mov 	%ax, %fs
	mov 	%ax, %gs
	# paging is on, and our code is still running at 0x00100000.
	# do some miscellaneous OS setup.  the coreid stuff is so we can call
	# core_id() before smp_boot. 
	movabs 	$(os_coreid_lookup), %rax
	movl	$0x0, (%rax)
	movabs 	$(hw_coreid_lookup), %rax
	movl	$0x0, (%rax)
	# Clear the frame pointer for proper backtraces
	movq	$0x0, %rbp
 	movabs	$(bootstacktop), %rsp
	movabs 	$(num_cpus), %rax
	movl	$0x1, (%rax)
	# Pass multiboot info to kernel_init (%rdi == arg1)
	movq 	%rbx, %rdi
	movabs	$(kernel_init), %rax
	call	*%rax
	# Should never get here, but in case we do, just spin.
spin:	jmp	spin

.section .bootdata, "aw"
	.p2align	2		# force 4 byte alignment
.globl gdt64
gdt64:
	SEG_NULL
	SEG_CODE_64(0)		# kernel code segment
	SEG_CODE_64(3)		# user code segment
	SEG_NULL			# these two nulls are a placeholder for the TSS
	SEG_NULL			# these two nulls are a placeholder for the TSS
	SEG_DATA_64			# generic flat data segment (debugging for now)
.globl gdt64desc
gdt64desc:
	.word	(gdt64desc - gdt64 - 1)		# sizeof(gdt64) - 1
	.long	gdt64		# HW 0-extends this to 64 bit when loading (i think)
# boot page tables
	.align PGSIZE
.globl boot_pml4
boot_pml4:
	.space  PGSIZE
boot_pml3_lo:
	.space  PGSIZE
boot_pml3_hi:
	.space  PGSIZE
boot_pml3_kernbase:
	.space  PGSIZE

# From here down is linked for KERNBASE
.data
	.p2align	PGSHIFT		# force page alignment
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:
