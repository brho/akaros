#include <arch/mmu.h>
#include <arch/sparc.h>
#include <arch/trap.h>
#include <ros/memlayout.h>
#include <arch/trap_table.h>

#define MAKE_STACK \
	mov	CORE_ID_REG,%l1; \
	sll	%l1,2,%l1; \
	set	core_stacktops,%l2; \
	ld	[%l2 + %l1],%l1; \
	set	SIZEOF_TRAPFRAME_T + 64,%l2; \
	sub	%l1,%l2,%fp

! preconditions:
! WIM & (1<<CWP) != 0
! link address in %l7
! postconditions:
! CWP same, but is now valid
! %l0, %l1, %l2, %l5, %l6, %l7 have not changed 
.global handle_window_overflow
handle_window_overflow:
	! clear out MMU faults (workaround for HW bug :()
	mov	0x300,%l3
	lda	[%l3] 4,%g0

	mov	%g1,%l4
	mov	%wim,%l3
	mov	%g0,%wim
	and	%l3,1,%g1

	! this will be patched at runtime; 0 is really NWINDOWS-1
.global spill_patchme
spill_patchme:
	sll	%g1,0,%g1

	srl	%l3,1,%l3
	or	%g1,%l3,%g1
	mov	%psr,%l3

	save
	mov	%g1,%wim
	btst	7,%sp
	bne	2f
	 lda	[%g0] 4,%g1		! do the fill in no-fault mode
	or	%g1,2,%g1		! NF = 1
	sta	%g1,[%g0] 4
	std	%l0,[%sp+ 0]
	std	%l2,[%sp+ 8]
	std	%l4,[%sp+16]
	std	%l6,[%sp+24]
	std	%i0,[%sp+32]
	std	%i2,[%sp+40]
	std	%i4,[%sp+48]
	std	%i6,[%sp+56]
	restore

	xor	%g1,2,%g1
	sta	%g1,[%g0] 4		! NF = 0
	mov	0x300,%g1
	lda	[%g1] 4,%g1
	btst	0x1C,%g1		! FT != 0 ?
	bne	1f
	 mov	%l4,%g1

	// success!
	mov	%l3,%psr
	jmp	%l1
	 rett	%l2

1:	// page fault
	mov	%l3,%psr
	MAKE_STACK
	TRAP_TABLE_ENTRY(spill_pagefault)

2:	// spill misaligned
	restore
	mov	%l3,%psr
	mov	%l4,%g1
	MAKE_STACK
	TRAP_TABLE_ENTRY(spill_misaligned)

.global handle_window_underflow
handle_window_underflow:
	! clear out MMU faults (workaround for HW bug :()
	mov	0x300,%l3
	lda	[%l3] 4,%g0

	mov	%wim,%l3
	mov	%g1,%l4
	mov	%psr,%l5
	mov	%g0,%wim

.global fill_patchme
fill_patchme:
	srl	%l3,0,%l7		! srl %l3,NWINDOWS-1,%l3
	and	%l7,1,%l7
	sll	%l3,1,%l6
	or	%l7,%l6,%l6

	restore
	restore
	btst	7,%sp
	bne	2f
	 lda	[%g0] 4,%g1		! do the fill in no-fault mode
	or	%g1,2,%g1		! NF = 1
	sta	%g1,[%g0] 4
	ldd	[%sp+ 0],%l0
	ldd	[%sp+ 8],%l2
	ldd	[%sp+16],%l4
	ldd	[%sp+24],%l6
	ldd	[%sp+32],%i0
	ldd	[%sp+40],%i2
	ldd	[%sp+48],%i4
	ldd	[%sp+56],%i6
	save
	save

	xor	%g1,2,%g1
	sta	%g1,[%g0] 4		! NF = 0
	mov	0x300,%g1
	lda	[%g1] 4,%g1
	btst	0x1C,%g1		! FT != 0 ?
	bne	1f
	 mov	%l4,%g1

	// success!
	mov	%l5,%psr
	mov	%l6,%wim
	jmp	%l1
	 rett	%l2

1:	// page fault
	mov	%l3,%wim
	mov	%l5,%psr
	MAKE_STACK
	TRAP_TABLE_ENTRY(fill_pagefault)

2:	// fill misaligned
	save
	save
	mov	%l4,%g1
	mov	%l3,%wim
	mov	%l5,%psr
	MAKE_STACK
	TRAP_TABLE_ENTRY(fill_misaligned)
