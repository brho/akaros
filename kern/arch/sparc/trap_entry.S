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
        set     bootstacktop-64-SIZEOF_TRAPFRAME_T,%l6
        mov     CORE_ID_REG,%l7
	sll	%l7,KSTKSHIFT,%l7
        sub     %l6,%l7,%l6

	// see if trap came from kernel; if so, use that stack
	// also preserve the psr, since we'll screw with the condition codes
	mov	%psr,%l7
	btst	PSR_PS,%l7
	bne,a	1f
	 sub	%fp,64+SIZEOF_TRAPFRAME_T,%l6

	// here is where we might do something with an active message

1:	mov	%l7,%l0
	// At this point we may use %l3/4/5/7 as temporary regs

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

2:	mov	%l0,%psr
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
	set	NWINDOWS,%g1
	ld	[%g1],%g1
	sub	%g1,1,%g2
5:	deccc	%g2
	bne,a	5b
tflush:	 save	%sp,0,%sp

	// get our old cwp back (restore should NOT trap)
	sub	%g1,1,%g2
6:	deccc	%g2
	bne,a	6b
	 restore

	// restore interrupt level
	wr	%l0,PSR_ET,%psr

	// call the handler and pass in the tf, message, and handler addr
	add	%sp,64,%o0
	call	trap
	 mov	%l5,%o1

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

	set	0x300,%o4
	set	0x400,%o5
	lda	[%o4] 4,%o4
	lda	[%o5] 4,%o5
	std	%o4,[%o0+152]
	lda	[%g0] 2,%o4
	mov	4,%o5
	lda	[%o5] 2,%o5
	std	%o4,[%o0+160]

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

	.global	env_pop_tf
env_pop_tf:

	// disable interrupts.
	// since wrpsr/rdpsr is interruptible, all interrupt handlers
	// must restore the psr to its interrupt-time value
	mov	%psr,%o1
	or	%o1,PSR_PIL,%o2
	wr	%o2,%psr

	// spill all kernel windows to the stack
	set	NWINDOWS,%g1
	ld	[%g1],%g1
	sub	%g1,1,%g2
5:	deccc	%g2
	bne,a	5b
	 save	%sp,0,%sp

	// get our old cwp back (restore should NOT trap)
	sub	%g1,1,%g2
6:	deccc	%g2
	bne,a	6b
	 restore
	

	// disable traps, and restore interrupt level
	wr	%o2,PSR_ET,%psr
	wr	%o1,PSR_ET,%psr

	// if we did this right (and didn't get an NMI!),
	// then (CWP+1) % NWINDOWS should be invalid.  fix that.
	save
	mov	%i0,%o0

#if 0
	// is (CWP+1) % NWINDOWS valid?
	and	%o1,PSR_CWP,%o2
	set	NWINDOWS,%o1
	ld	[%o1],%o1
	add	%o2,1,%o2
	cmp	%o2,%o1
	be,a	3f
	 mov	0,%o2

3:	mov	1,%o3
	sll	%o3,%o2,%o2		! %o2 = 1 << ((CWP+1)%NWINDOWS)
	mov	%wim,%o3
	btst	%o2,%o3			! (%wim & %o2) == 0?
	be	4f
	 nop

	sethi	%hi(fill),%o3		! gotta fill a window
	jmpl	%lo(fill)+%o3,%o3
	 save	%o3,0,%l7		! fill returns to %l7
	restore

#endif

	// restore user context
4:	mov	%o0,%g2
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

	ldd	[%o0+128],%l0
	mov	%psr,%l2
	and	%l2,PSR_CWP,%l2
	andn	%l0,PSR_CWP,%l0
	wr	%l2,%l0,%psr

	ldd	[%o0+136],%l2
	!mov	%l3,%wim
	ldd	[%o0+144],%l4
	mov	%l4,%tbr
	mov	%l5,%y
	nop

	jmp	%l1
	rett	%l2

	.global send_active_message_raw
send_active_message_raw:
	retl
	 .word	0x81f00000
