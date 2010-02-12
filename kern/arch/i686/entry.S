/* See COPYRIGHT for copyright information. */

#include <arch/mmu.h>
#include <arch/trap.h>
#include <ros/memlayout.h>

# Shift Right Logical 
#define SRL(val, shamt)		(((val) >> (shamt)) & ~(-1 << (32 - (shamt))))


###################################################################
# The kernel (this code) is linked at address (KERNBASE + 0x00100000 (1MB)), 
# but we tell the bootloader to load it at physical address 
# 0x00100000 (1MB), which is the start of extended memory.
# (See kernel.ld)
###################################################################


###################################################################
# RELOC(x) maps a symbol x from its link address to its actual
# location in physical memory (its load address).	 
###################################################################
#define	RELOC(x) ((x) - KERNBASE)


.set CODE_SEL,0x8		# index of code seg within mygdt
.set DATA_SEL,0x10		# index of data seg within mygdt

#define MULTIBOOT_PAGE_ALIGN  (1<<0)
#define MULTIBOOT_MEMORY_INFO (1<<1)
#define MULTIBOOT_HEADER_MAGIC (0x1BADB002)
#define MULTIBOOT_HEADER_FLAGS (MULTIBOOT_MEMORY_INFO | MULTIBOOT_PAGE_ALIGN)
#define CHECKSUM (-(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS))

###################################################################
# entry point
###################################################################

.text

# The Multiboot header
.align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long CHECKSUM

.globl		_start
_start:
	movw	$0x1234,0x472			# warm boot

	# Establish our own GDT in place of the boot loader's temporary GDT.
	lgdt	RELOC(mygdtdesc)		# load descriptor table

	# Immediately reload all segment registers (including CS!)
	# with segment selectors from the new GDT.
	movl	$DATA_SEL, %eax			# Data segment selector
	movw	%ax,%ds				# -> DS: Data Segment
	movw	%ax,%es				# -> ES: Extra Segment
	movw	%ax,%ss				# -> SS: Stack Segment
	ljmp	$CODE_SEL,$relocated		# reload CS by jumping
relocated:
	movl	$0x0, os_coreid_lookup
	movl	$0x0, hw_coreid_lookup

	# Clear the frame pointer register (EBP)
	# so that once we get into debugging C code,
	# stack backtraces will be terminated properly.
	movl	$0x0,%ebp			# nuke frame pointer

    # Set the stack pointer
 	movl	$(bootstacktop),%esp

	# Save multiboot info
	push 	%ebx

	# now to C code
	movl	$0x1,num_cpus 		# init global var, for now
	call	kernel_init

	# Should never get here, but in case we do, just spin.
spin:	jmp	spin

###################################################################	
# See <inc/memlayout.h> for a complete description of these two symbols.
###################################################################
.data
	.globl	vpt
	.set	vpt, VPT
	.globl	vpd
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

###################################################################
# setup the GDT	
###################################################################
	.p2align	2		# force 4 byte alignment
mygdt:
	SEG_NULL				# null seg
	SEG(STA_X|STA_R, -KERNBASE, 0xffffffff)	# code seg
	SEG(STA_W, -KERNBASE, 0xffffffff)	# data seg
mygdtdesc:
	.word	0x17			# sizeof(mygdt) - 1
	.long	RELOC(mygdt)		# address mygdt

