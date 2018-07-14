/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <arch/mmu.h>
#include <arch/trap.h>
#include <arch/x86.h>
#include <kstack.h>

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

# Calling convention for internal functions:
#
# my convention:
# 	callee saved ebp, ebx
# 	caller saves eax ecx edx esi edi
# 	args: a0 edi, a1 esi, a2 edx, a3 ecx, a4 eax, a5+ stack
# 	ret eax
#
# for reference, the normal convention:
# 	callee saved: esi, edi, ebp, ebx
# 	caller saved: eax, ecx, edx
# 	args on stack
# 	ret eax

/* Helper: creates count mappings in the PML3 for 1GB jumbo pages for the given
 * vaddr to paddr range in physical memory.  Then it puts that PML3's addr in
 * the PML4's appropriate slot.  Using a macro mostly to help with 64 bit
 * argument marshalling.
 *
 * This will clobber ax, dx, cx, di, si.
 *
 * A few notes about the jumbo GB mapping:
 * 	- PML3 is responsible for the 9 bits from 30-38, hence the >> 30 and mask
 * 	- PML4 is responsible for the 9 bits from 47-39, hence the >> 39 and mask
 * 	- We use the jumbo PTE_PS flag only on PML3 - can't do it for PML4.
 * 	- PTEs are 8 bytes each, hence the scale = 8 in the indirect addressing
 * 	- The top half of all of PML4's PTEs are set to 0.  This includes the top 20
 * 	bits of the physical address of the page tables - which are 0 in our case.
 * 	- The paddr for the PML3 PTEs is split across two 32-byte halves of the PTE.
 * 	We drop off the lower 30 bits, since we're dealing with 1GB pages.  The 2
 * 	LSBs go at the top of the first half of the PTE, and the remaining 30 are
 * 	the lower 30 of the top half. */
#define MAP_GB_PAGES(pml3, vaddr, paddr, count)                                \
	movl	$(boot_pml4), %eax;                                                \
	push	%eax;                                                              \
	movl	$(count), %eax;                                                    \
	push	%eax;                                                              \
	movl	$(pml3), %edi;                                                     \
	movl	$(vaddr >> 32), %esi;                                              \
	movl	$(vaddr & 0xffffffff), %edx;                                       \
	movl	$(paddr >> 32), %ecx;                                              \
	movl	$(paddr & 0xffffffff), %eax;                                       \
	call	map_gb_pages;                                                      \
	add		$0x8, %esp

# Maps count GBs (up to 512) of vaddr -> paddr using pml3 and pml4 in 1GB pages
#
# edi pml3, esi vaddr_hi, edx vaddr_lo, ecx paddr_hi, eax paddr_lo,
# stack: count, pml4
map_gb_pages:
	push	%ebx
	movl	0x8(%esp), %ebx
	# save these 3, need them for the next call
	push	%edi
	push	%esi
	push	%edx
	# arg5 on stack.  other args already in regs.
	push	%ebx
	call	fill_jpml3
	add		$0x4, %esp 		# pop arg5 frame
	# restore our regs/args for next call
	pop		%edx
	pop		%esi
	pop		%edi
	movl	0xc(%esp), %ecx
	call	insert_pml3
	pop		%ebx
	ret

# Fills pml3 with "count" jumbo entries, mapping from vaddr -> paddr.
# pml3s are responsible for bits 38..30 of vaddr space and 30 bit paddr entries
#
# edi pml3, esi vaddr_hi, edx vaddr_lo, ecx paddr_hi, eax paddr_lo, stack count
fill_jpml3:
	push	%ebx
	movl	0x8(%esp), %ebx
	# want (vaddr >> 30) & 0x1ff into esi.  append upper 2 bits of edx to esi.
	shll	$2, %esi
	shrl	$30, %edx
	orl		%edx, %esi
	andl	$0x1ff, %esi
	# want (paddr >> 30) into ecx.
	shll	$2, %ecx
	shrl	$30, %eax
	orl		%eax, %ecx
1:
	movl	%ecx, %eax
	shll	$30, %eax					# lower part of PTE ADDR
	orl		$(PTE_P | PTE_W | PTE_PS | PTE_G), %eax
	movl	%eax, (%edi, %esi, 8)
	movl	%ecx, %eax
	shrl	$2, %eax					# upper part of PTE ADDR
	movl	%eax, 4(%edi, %esi, 8)
	# prep for next loop
	incl	%esi
	incl	%ecx
	decl	%ebx
	jnz		1b
	pop		%ebx
	ret

#define MAP_2MB_PAGES(pml3, vaddr, paddr, count, pml2base)                     \
	movl	$(pml2base), %eax;                                                 \
	push	%eax;                                                              \
	movl	$(boot_pml4), %eax;                                                \
	push	%eax;                                                              \
	movl	$(count), %eax;                                                    \
	push	%eax;                                                              \
	movl	$(pml3), %edi;                                                     \
	movl	$(vaddr >> 32), %esi;                                              \
	movl	$(vaddr & 0xffffffff), %edx;                                       \
	movl	$(paddr >> 32), %ecx;                                              \
	movl	$(paddr & 0xffffffff), %eax;                                       \
	call	map_2mb_pages;                                                     \
	add		$0xc, %esp

# Maps count GBs (up to 512) of vaddr -> paddr using pml3, pml4, and an array of
# pml2s in 2MB pages
#
# edi pml3, esi vaddr_hi, edx vaddr_lo, ecx paddr_hi, eax paddr_lo,
# stack: count, pml4, pml2_base
map_2mb_pages:
	push	%ebx
	# save these 3, need them for the next call
	push	%edi
	push	%esi
	push	%edx
	# arg5 and 7 on stack.  other args already in regs.
	movl	0x1c(%esp), %ebx	# arg7 (4 pushes, 1 retaddr, arg 5, arg6)
	push	%ebx
	movl	0x18(%esp), %ebx	# arg6 (5 pushes, 1 retaddr)
	push	%ebx
	call	fill_pml3
	add		$0x8, %esp 			# pop args frame
	# restore our regs/args for next call
	pop		%edx
	pop		%esi
	pop		%edi
	movl	0xc(%esp), %ecx
	call	insert_pml3
	pop		%ebx
	ret

# Fills pml3 with "count" pml2 entries, mapping from vaddr -> paddr.
# pml3s are responsible for bits 38..30 of vaddr space and 30 bit paddr entries
#
# edi pml3, esi vaddr_hi, edx vaddr_lo, ecx paddr_hi, eax paddr_lo,
# stack count, pml2base
fill_pml3:
	push	%ebx
	push	%ebp						# scratch register
	movl	0xc(%esp), %ebx
1:
	push	%edi						# save edi = pml3
	push	%esi
	push	%edx
	push	%ecx
	push	%eax
	movl	$512, %ebp					# count = 512 for PML2 (map it all)
	push	%ebp
	# compute pml2 (pml2base + (total count - current count) * PGSIZE * 2)
	movl	0x28(%esp), %ebp			# pml2base (8 push, 1 ret, arg5)
	movl	0x24(%esp), %edi			# total count
	subl	%ebx, %edi
	shll	$13, %edi
	addl	%edi, %ebp
	movl	%ebp, %edi					# arg0 for the func call
	call	fill_jpml2
	add		$0x4, %esp
	pop		%eax
	pop		%ecx
	pop		%edx
	pop		%esi
	pop		%edi
	# re-save our register frame
	push	%edi
	push	%esi
	push	%edx
	push	%ecx
	push	%eax
	# prep call to insert (ecx = pml3, edi = pml2)
	movl	%edi, %ecx
	movl	%ebp, %edi
	call	insert_pml2
	pop		%eax
	pop		%ecx
	pop		%edx
	pop		%esi
	pop		%edi
	# prep for next loop.  need to advance vaddr and paddr by 1GB
	addl	$(1 << 30), %edx
	adcl 	$0, %esi
	addl	$(1 << 30), %eax
	adcl 	$0, %ecx
	decl	%ebx
	jnz		1b
	pop		%ebp
	pop		%ebx
	ret

# Fills pml2 with "count" jumbo entries, mapping from vaddr -> paddr
# pml2s are responsible for bits 29..21 of vaddr space and 21 bit paddr entries
#
# edi pml2, esi vaddr_hi, edx vaddr_lo, ecx paddr_hi, eax paddr_lo, stack count
fill_jpml2:
	push	%ebx
	movl	0x8(%esp), %ebx
	# want (vaddr >> 21) & 0x1ff into esi.
	shrl	$21, %edx
	movl	%edx, %esi
	andl	$0x1ff, %esi
	# want (paddr >> 21) into ecx.
	shll	$11, %ecx
	shrl	$21, %eax
	orl		%eax, %ecx
1:
	movl	%ecx, %eax
	shll	$21, %eax					# lower part of PTE ADDR
	orl		$(PTE_P | PTE_W | PTE_PS | PTE_G), %eax
	movl	%eax, (%edi, %esi, 8)
	movl	%ecx, %eax
	shrl	$11, %eax					# upper part of PTE ADDR
	movl	%eax, 4(%edi, %esi, 8)
	# prep for next loop
	incl	%esi
	incl	%ecx
	decl	%ebx
	jnz		1b
	pop		%ebx
	ret

# Inserts a pml3 into pml4, so that it handles mapping for vaddr
#
# edi pml3, esi vaddr_hi, edx vaddr_lo, ecx pml4
insert_pml3:
	shrl	$7, %esi 	# want to shift vaddr >> 39
	andl	$0x1ff, %esi
	orl		$(PTE_P | PTE_W | PTE_G), %edi
	movl	%edi, (%ecx, %esi, 8)
	movl	$0x0, 4(%ecx, %esi, 8)	# being clever, i know upper bits are 0
	ret

# Inserts a pml2 into pml3, so that it handles mapping for vaddr
#
# edi pml2, esi vaddr_hi, edx vaddr_lo, ecx pml3
insert_pml2:
	# want (vaddr >> 30) & 0x1ff into esi.  append upper 2 bits of edx to esi.
	shll	$2, %esi
	shrl	$30, %edx
	orl		%edx, %esi
	andl	$0x1ff, %esi
	orl		$(PTE_P | PTE_W | PTE_G), %edi
	movl	%edi, (%ecx, %esi, 8)
	movl	$0x0, 4(%ecx, %esi, 8)	# being clever, i know upper bits are 0
	ret

.globl		_start
_start:
	movl	$stack32top, %esp
	push	%ebx					# save mulitboot info
	movw	$0x1234,0x472			# warm boot
	movl	$0x80000001, %eax
	# some machines / VMs might not support long mode
	cpuid
	test	$(1 << 29), %edx
	jz		err_no_long
	# others don't support 1GB jumbo pages, which is a shame
	test	$(1 << 26), %edx
	jz		no_pml3ps
	# build page table.  need mappings for
	# 	- current code/data at 0x00100000 -> 0x00100000
	#	- kernel load location: 0xffffffffc0000000 -> 0x0000000000000000
	#	- kernbase: 0xffff80000000 -> 0x0000000000000000
	# we'll need one table for the PML4, and three PML3 (PDPE)'s.  1GB will
	# suffice for lo and hi (til we do the VPT and LAPIC mappings).  For
	# kernbase, we'll do all 512 PML3 entries (covers 512GB)
	MAP_GB_PAGES(boot_pml3_lo, 0x0000000000000000, 0x0, 1)
	MAP_GB_PAGES(boot_pml3_hi, 0xffffffffc0000000, 0x0, 1)
	MAP_GB_PAGES(boot_pml3_kb, 0xffff800000000000, 0x0, 512)
	jmp		post_mapping
no_pml3ps:
	MAP_2MB_PAGES(boot_pml3_lo, 0x0000000000000000, 0x0,   1, boot_pml2_lo)
	MAP_2MB_PAGES(boot_pml3_hi, 0xffffffffc0000000, 0x0,   1, boot_pml2_hi)
	MAP_2MB_PAGES(boot_pml3_kb, 0xffff800000000000, 0x0, 512, boot_pml2_kb)
post_mapping:
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
	pop		%ebx				# restore multiboot info
	# load the 64bit GDT and jump to long mode
	lgdt	gdt64desc
	ljmp	$0x08, $long_mode
	# these are error handlers, we're jumping over these
err_no_long:
	mov		$no_long_string, %esi
	jmp		printstring
err_no_pml3ps:
	mov		$no_pml3ps_string, %esi
	jmp		printstring
printstring:
	mov		$0xb8a00, %edi		# assuming CGA buffer, 16 lines down
	mov		$0, %ecx
1:
	movb	(%esi, %ecx), %bl
	test	%bl, %bl
	je		printdone
	# print to the console (0x07 is white letters on black background)
	mov		$0x07, %bh
	mov		%bx, (%edi, %ecx, 2)
	# print to serial
	mov		$(0x3f8 + 5), %edx	# assuming COM1
2:
	inb		%dx, %al
	test	$0x20, %al			# ready check
	jz		2b
	mov		$0x3f8, %edx		# assuming COM1
	mov		%bl, %al
	outb	%al, %dx
	# advance the loop
	inc		%ecx
	jmp		1b
printdone:
	hlt
	jmp		printdone

.code64
long_mode:
	# zero the data segments.  Not sure if this is legit or not.
	xor		%rax, %rax
	mov		%ax, %ds
	mov		%ax, %es
	mov		%ax, %ss
	mov		%ax, %fs
	mov		%ax, %gs
	lldt	%ax
	# paging is on, and our code is still running at 0x00100000.
	# do some miscellaneous OS setup.
	# set up gs to point to our pcpu info (both GS base and KERN GS base)
	movabs	$(per_cpu_info), %rdx
	movq	%rdx, %rax
	shrq	$32, %rdx
	andl	$0xffffffff, %eax
	movl	$MSR_GS_BASE, %ecx
	wrmsr
	movl	$MSR_KERN_GS_BASE, %ecx
	wrmsr
	# Clear the frame pointer for proper backtraces
	movq	$0x0, %rbp
	# We can use the bootstack from the BSS, but we need the KERNBASE addr
	movabs	$(bootstacktop + KERNBASE), %rsp
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
	# keep the number of these in sync with SEG_COUNT
	SEG_NULL
	SEG_CODE_64(0)		# kernel code segment
	SEG_DATA_64(0)		# kernel data segment
	SEG_DATA_64(3)		# user data segment
	SEG_CODE_64(3)		# user code segment
	SEG_NULL			# these two nulls are a placeholder for the TSS
	SEG_NULL			# these two nulls are a placeholder for the TSS
.globl gdt64desc
gdt64desc:
	.word	(gdt64desc - gdt64 - 1)		# sizeof(gdt64) - 1
	.long	gdt64		# HW 0-extends this to 64 bit when loading (i think)
no_long_string:
	.string "Unable to boot: long mode not supported"
no_pml3ps_string:
	.string "Unable to boot: 1 GB pages not supported"
# boot page tables
.section .bootbss, "w",@nobits
	.align PGSIZE
.globl boot_pml4
boot_pml4:
	.space  PGSIZE * 2
boot_pml3_lo:
	.space  PGSIZE * 2
boot_pml3_hi:
	.space  PGSIZE * 2
boot_pml3_kb:
	.space  PGSIZE * 2
stack32:
	.space  PGSIZE
stack32top:
# Could make all of the no-jumbo stuff a config var
boot_pml2_lo:		# one pml2 (1GB in the lo pml3)
	.space  PGSIZE * 2
boot_pml2_hi:		# one pml2 (1GB in the hi pml3)
	.space  PGSIZE * 2
boot_pml2_kb: 		# 512 pml2s (+ epts) in the kb pml3
	.space  PGSIZE * 512 * 2

# From here down is linked for KERNBASE
.text
	.globl get_boot_pml4
get_boot_pml4:
	movabs	$(boot_pml4), %rax
	ret
	.globl get_gdt64
get_gdt64:
	movabs	$(gdt64), %rax
	ret
.section .bootbss, "w",@nobits
	.p2align	PGSHIFT		# force page alignment
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop
bootstacktop:
