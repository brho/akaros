#include <arch/sparc.h>
#include <arch/trap.h>
#include <arch/mmu.h>
#include <arch/trap_table.h>
#include <ros/memlayout.h>


// Macro to save a minimal part of the trap frame, i.e., what's necessary
// to safely return from interrupt.  Global registers, y, pc, npc, far, fsr.
#define SAVE_MINIMAL_TF(tf) \
	std	%g0,[tf+  0] ;\
	std	%g2,[tf+  8] ;\
	std	%g4,[tf+ 16] ;\
	std	%g6,[tf+ 24] ;\
	st	%l7,[tf+128] ;\
	st	%l5,[tf+132] ;\
	st	%l6,[tf+136] ;\
	mov	%tbr,%g2     ;\
	mov	%y,%g3       ;\
	std	%g2,[tf+144] ;\
	set	0x300,%g2    ;\
	set	0x400,%g3    ;\
	lda	[%g2] 4,%g2  ;\
	lda	[%g3] 4,%g3  ;\
	std	%g2,[tf+152]

// Macro to restore same.
#define RESTORE_MINIMAL_TF(tf) \
	ld	[tf+  4],%g1 ;\
	ldd	[tf+  8],%g2 ;\
	ldd	[tf+ 16],%g4 ;\
	ldd	[tf+ 24],%g6 ;\
	ld	[tf+148],%l0 ;\
	mov	%l0,%y           ;\
	ld	[tf+132],%l5 ;\
	ld	[tf+136],%l6 ;\
	jmp	%l5 ;\
	 rett	%l6


	.section	".text"!,#alloc,#execinstr,#progbits
	.align		4

	.global	tflush1
	.global	tflush2
	.global trap_patchme

	.global handle_trap
handle_trap:

	// First, make sure we have a valid register window.
	// The restore won't trap, but the save might.
	// The spill handler won't overwrite l0/l5/l6, so stash l1 (pc) and l2 (npc)
	// in there, since the hardware will overwrite them if save traps
	mov	%psr,%l7
	andn	%l7,PSR_PIL,%l6
	wr	%l6,PSR_PIL,%psr
	wr	%l6,PSR_PIL|PSR_ET,%psr

	// Are we user or kernel?
	btst	PSR_PS,%l7
	mov	%l2,%l6
	be	1f
	 mov	%l1,%l5

	// Trap came from kernel.  Spill a window if necessary.
	SAVE_MINIMAL_TF(%fp-SIZEOF_TRAPFRAME_T)
	mov	%tbr,%g1
	restore
	save

	// Set up stack, save state, call handler
	sub	%fp,96+SIZEOF_TRAPFRAME_T,%sp

	// Save full trap frame if this was a synchronous exception (=> bug!!)
	and	%g1,0xF00,%g1
	cmp	%g1,0x100
	be	2f
	 nop

	call	save_rest_of_tf
	 add	%sp,96,%o0

2:	call	%l0
	 add	%sp,96,%o0

	// Fill a window if necessary.
	restore
	save

	// Restore PSR, then GTFO
	ld	[%sp+96+128],%l7
	mov	%l7,%psr
	RESTORE_MINIMAL_TF(%sp+96)

	// Trap came from user.  Spill a window if necessary.
	// Set %l1 to be the stacktop for the current core
1:	mov	CORE_ID_REG,%l1
	sll	%l1,2,%l1
	set	core_stacktops,%l2
	ld	[%l2 + %l1],%l1
	set	SIZEOF_TRAPFRAME_T,%l2
	sub	%l1,%l2,%l1

	SAVE_MINIMAL_TF(%l1)
	sub	%l1,96,%g1
	restore
tflush1:
	save

	// Set up stack.
	// Save a full trap frame, since we might not return through this path
	mov	%g1,%sp
	call	save_rest_of_tf
	 add	%sp,96,%o0

	// spill all trapper's windows out to the stack.
	// the 'save' may trap (triggering the spill),
	// and if the stack is corrupted, the process may die
trap_patchme:
	mov	0,%g2						// 0 will become NWINDOWS-1
	mov	%g2,%g3
5:	deccc	%g2
	bne,a	5b
tflush2:
	 save

	// get our old window back
5:	deccc	%g3
	bne,a	5b
	 restore

	// Call the handler
	call	%l0
	 add	%sp,96,%o0

	// fallthrough to env_pop_tf, which is right below this function
	add	%sp,96,%o0

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

	RESTORE_MINIMAL_TF(%o0)

	// save_rest_of_tf saves what SAVE_MINIMAL_TF doesn't.
	// o0: destination trapframe_t*
save_rest_of_tf:

	mov	%wim,%o4
	st	%o4,[%o0+140]

	lda	[%g0] 2,%o4
	mov	4,%o5
	lda	[%o5] 2,%o5
	std	%o4,[%o0+160]

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

	.global handle_perfctr
handle_perfctr:
	andn	%i0,7,%i1
	lda	[%i1] 2,%i0
	add	%i1,4,%i1
	lda	[%i1] 2,%i1
	jmp	%l2
	 rett	%l2+4

	// we make cpu_halt a linker symbol so we know if we were halted.
	// if we were halted, we should return to PC+4, not PC.
	.global cpu_halt
cpu_halt:
	ba,a cpu_halt
	retl
	 nop
