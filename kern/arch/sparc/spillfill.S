#include <arch/mmu.h>
#include <arch/sparc.h>
#include <arch/trap.h>
#include <ros/memlayout.h>

// before spilling a window, we must be certain
// that %sp is 8-byte aligned and the range [%sp,%sp+64)
// is validly mapped in
#define VALIDATE_STACK(reg1,reg2,misaligned,pagefault)	\
	mov	%psr,reg1		;\
	btst	7,%sp			;\
	bne	misaligned		;\
	 mov	reg1,%psr		;\
	andn	%sp,0xFFF,reg1		;\
	or	reg1,0x400,reg1		;\
	lda	[reg1] 3,reg2		;\
	add	%sp,56,reg1		;\
	andn	reg1,0xFFF,reg1		;\
	or	reg1,0x400,reg1		;\
	lda	[reg1] 3,reg1		;\
	and	reg2,reg1,reg2		;\
	mov	%psr,reg1		;\
	btst	PTE_PTE,reg2		;\
	be	pagefault		;\
	 mov	reg1,%psr

#define RETHROW_TRAP(func)		\
	mov	%psr,%l7		;\
	and	%l7,PSR_CWP,%l4		;\
	set	NWINDOWS,%l3		;\
	ld	[%l3],%l3		;\
	dec	%l3			;\
	cmp	%l3,%l4			;\
	inc	%l4			;\
	be,a	7f			;\
	 mov	0,%l4			;\
7:	mov	1,%l3			;\
	sll	%l3,%l4,%l4		;\
	mov	%g0,%wim		;\
	set     bootstacktop-64-SIZEOF_TRAPFRAME_T,%sp	;\
	mov     CORE_ID_REG,%l5		;\
	sll     %l5,KSTKSHIFT,%l5	;\
	sub     %sp,%l5,%sp		;\
	btst	PSR_PS,%l7		;\
	bne,a	8f			;\
	 sub	%fp,64+SIZEOF_TRAPFRAME_T,%sp ;\
8:	mov	%l7,%psr		;\
	mov	%l1,%o1			;\
	mov	%l2,%o2			;\
	call	env_save_tf		;\
	 add	%sp,64,%o0		;\
	mov	%l4,%wim		;\
	mov	%psr,%o0		;\
	wr	%o0,PSR_ET,%psr		;\
	call	func			;\
	 add	%sp,64,%o0

	.global handle_window_overflow
handle_window_overflow:
	sethi	%hi(spill),%l7
	jmpl	%l7+%lo(spill),%l7
	 nop
	jmp	%l1
	rett	%l2

	.global handle_window_underflow
handle_window_underflow:
	sethi	%hi(fill),%l7
	jmpl	%l7+%lo(fill),%l7
	 nop
	jmp	%l1
	rett	%l2

! after handling a window trap, spill/fill will return to window_rett+8,
! so these two nops are necessary!
	.global	window_rett
window_rett:
	nop
	nop
	jmp	%l1
	rett	%l2

! preconditions:
! WIM & (1<<CWP) != 0
! link address in %l7
! postconditions:
! CWP same, but is now valid
! %l0, %l1, %l2, %l5, %l6 have not changed 
.global spill
spill:
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
	mov	%g2,%l3

	save
	mov	%g1,%wim
	VALIDATE_STACK(%g1,%g2,1f,2f)
	std	%l0,[%sp+ 0]
	std	%l2,[%sp+ 8]
	std	%l4,[%sp+16]
	std	%l6,[%sp+24]
	std	%i0,[%sp+32]
	std	%i2,[%sp+40]
	std	%i4,[%sp+48]
	std	%i6,[%sp+56]
	restore

	mov	%l3,%g2
	jmp	%l7+8
	mov	%l4,%g1

// spill failed!
1:	restore
	RETHROW_TRAP(stack_misaligned)
2:	restore
	RETHROW_TRAP(stack_pagefault)

! preconditions:
! WIM & (1<<((CWP+2)%NWINDOWS)) != 0
! link address in %l7
! postconditions:
! CWP same, but (CWP+2)%NWINDOWS now valid
! %l0, %l1, %l2, %l5, %l6 have not changed
.global fill 
fill:
	mov	%g1,%l4
	mov	%wim,%l3
	mov	%g0,%wim

	! this will be patched at runtime; 0 is really NWINDOWS-1
.global fill_patchme
fill_patchme:
	srl	%l3,0,%g1

	and	%g1,1,%g1
	sll	%l3,1,%l3
	or	%g1,%l3,%g1

	restore
	restore
	VALIDATE_STACK(%l0,%l1,3f,4f)
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
	mov	%g1,%wim
	nop

	jmp	%l7+8
	mov	%l4,%g1


// spill failed!
3:	save
	save
	RETHROW_TRAP(stack_misaligned)
4:	save
	save
	RETHROW_TRAP(stack_pagefault)
