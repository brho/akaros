#include <arch/sparc.h>
#include <arch/trap.h>
#include <arch/mmu.h>
#include <arch/trap_table.h>
#include <ros/memlayout.h>

	.section	".text"!,#alloc,#execinstr,#progbits
	.align		4

	.global tflush

	.global handle_trap
handle_trap:

	// At this point, %l1 = pc, %l2 = npc, and %l0/3/4/5
	// might contain an active message.  so we only get %l6/%l7

	// calculate stack pointer (-64 is space for window spill).
	// sp = bootstacktop - core_id*KSTKSIZE - 64 - sizeof(trapframe_t).
	// should you change this, make sure to change stack_fucked()
	set	bootstacktop-64-SIZEOF_TRAPFRAME_T,%l6
	mov	CORE_ID_REG,%l7
	sll	%l7,KSTKSHIFT,%l7
	sub	%l6,%l7,%l6

	// see if trap came from kernel; if so, use that stack
	// also preserve the psr, since we'll screw with the condition codes
	mov	%psr,%l0
	btst	PSR_PS,%l0
	bne,a	1f
	 sub	%fp,64+SIZEOF_TRAPFRAME_T,%l6

	// here is where we might do something with an active message

	// At this point we may use %l3/4/5/7 as temporary regs
1:
	// is CWP valid?
	and	%l0,PSR_CWP,%l4	! %l4 = cwp
	mov	1,%l7
	sll	%l7,%l4,%l4	! %l4 = 1 << cwp
	mov	%wim,%l3
	btst	%l3,%l4		! (%wim & %l4) == 0?
	be	2f
	 sethi	%hi(spill),%l7
	jmpl	%lo(spill)+%l7,%l7	!no, spill a window
	 nop

	// At this point we may use %o0-7, %l3/4/5/7 as temporary regs
	// save the whole user context to a trapframe_t

2:
	mov	%l6,%sp
	mov	%l1,%o1
	mov	%l2,%o2
	call	env_save_tf
	 add	%sp,64,%o0

	// enable traps (but not interrupts)
	or	%l0,PSR_PIL,%l3
	wr	%l3,0,%psr
	wr	%l3,PSR_ET,%psr

	// spill all trapper's windows out to the stack.
	// the 'save' may trap (triggering the spill),
	// and if the stack is corrupted, the process may die
	mov	%l0,%g3
	mov	%sp,%g4

	set	NWINDOWS,%g1
	ld	[%g1],%g1
	sub	%g1,1,%g2
5:	deccc	%g2
	bne,a	5b
tflush:	 save	%sp,0,%sp

	// restore interrupt level
	mov	%g4,%sp	
	wr	%g3,PSR_ET,%psr

	// call the handler and pass in the tf and handler address
	call	%l5
	 add	%sp,64,%o0

	// should never get here
	unimp

// void env_save_tf(trapframe_t* tf, uint32_t trap_pc, uint32_t trap_npc)
	.global env_save_tf
env_save_tf:

	mov	%psr,%o4
	st	%o4,[%o0+128]
	st	%o1,[%o0+132]
	st	%o2,[%o0+136]
	mov	%wim,%o4
	st	%o4,[%o0+140]
	mov	%tbr,%o4
	mov	%y,%o5
	std	%o4,[%o0+144]
	mov	%asr13,%o5
	st	%o5,[%o0+152]

	set	0x300,%o4
	set	0x400,%o5
	lda	[%o4] 4,%o4
	lda	[%o5] 4,%o5
	std	%o4,[%o0+160]
	lda	[%g0] 2,%o4
	mov	4,%o5
	lda	[%o5] 2,%o5
	std	%o4,[%o0+168]

	std	%g0,[%o0+ 0]
	std	%g2,[%o0+ 8]
	std	%g4,[%o0+16]
	std	%g6,[%o0+24]

	mov	%o0,%g2

	restore
	std	%o0,[%g2+32]
	std	%o2,[%g2+40]
	std	%o4,[%g2+48]
	std	%o6,[%g2+56]
	std	%l0,[%g2+64]
	std	%l2,[%g2+72]
	std	%l4,[%g2+80]
	std	%l6,[%g2+88]
	std	%i0,[%g2+96]
	std	%i2,[%g2+104]
	std	%i4,[%g2+112]
	std	%i6,[%g2+120]
	save

	retl
	 nop

	// this routine only works for returning to userspace.
	// right now, there's no mechanism to resume kernel operation after
	// a fault
	.global	env_pop_tf
env_pop_tf:

	mov	%psr,%o1
	wr	%o1,PSR_ET,%psr

	// CWP = 0 (therefore 1 after rett), so set window 2 invalid
	mov	1<<2,%wim
	ld	[%o0+128],%o1
	mov	%o0,%g2
	andn	%o1,PSR_CWP,%o1
	mov	%o1,%psr
	nop; nop; nop
	mov	%g2,%o0

4:	// restore user context
	restore
	ldd	[%g2+32],%o0
	ldd	[%g2+40],%o2
	ldd	[%g2+48],%o4
	ldd	[%g2+56],%o6
	ldd	[%g2+64],%l0
	ldd	[%g2+72],%l2
	ldd	[%g2+80],%l4
	ldd	[%g2+88],%l6
	ldd	[%g2+96],%i0
	ldd	[%g2+104],%i2
	ldd	[%g2+112],%i4
	ldd	[%g2+120],%i6
	save

	ld	[%o0+ 4],%g1
	ldd	[%o0+ 8],%g2
	ldd	[%o0+16],%g4
	ldd	[%o0+24],%g6

	ld	[%o0+132],%l1
	ld	[%o0+136],%l2
	ld	[%o0+148],%l5
	mov	%l5,%y
	ld	[%o0+152],%l5
	mov	%l5,%asr13

	jmp	%l1
	rett	%l2

	.global handle_perfctr
handle_perfctr:
	andn	%i0,7,%i1
	lda	[%i1] 2,%i0
	add	%i1,4,%i1
	lda	[%i1] 2,%i1
	jmp	%l2
	 rett	%l2+4
