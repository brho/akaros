#include <arch/sparc.h>
#include <arch/trap.h>
#include <arch/mmu.h>
#include <arch/trap_table.h>
#include <ros/memlayout.h>

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
	wr	%l7,PSR_PIL,%psr
	wr	%l7,PSR_PIL|PSR_ET,%psr

	// Are we user or kernel?
	btst	PSR_PS,%l7
	mov	%l2,%l6
	be	1f
	 mov	%l1,%l5

	// Trap came from kernel.  Spill a window if necessary.
	restore
	save

	// Save TF then call handler
	sub	%fp,64+SIZEOF_TRAPFRAME_T,%sp
	call	env_save_tf
	 add	%sp,64,%o0
	call	%l0
	 add	%sp,64,%o0

#define FINISH_POP_TF(tf) \
	ld	[tf+132],%l1 ;\
	ld	[tf+136],%l2 ;\
	ld	[tf+148],%l5 ;\
	mov	%l5,%y ;\
	jmp	%l1 ;\
	rett	%l2

	// Get outta here! Fill a window if necessary.
	restore
	save
	mov	%l7,%psr
	add	%sp,64,%o0
	FINISH_POP_TF(%o0)

	// Trap came from user.  Spill a window if necessary.
1:	restore
tflush1:
	save

	mov	CORE_ID_REG,%l1
	sll	%l1,KSTKSHIFT,%l1
	set	bootstacktop-64-SIZEOF_TRAPFRAME_T,%sp
	sub	%sp,%l1,%sp

	call	env_save_tf
	 add	%sp,64,%o0

	// spill all trapper's windows out to the stack.
	// the 'save' may trap (triggering the spill),
	// and if the stack is corrupted, the process may die
	mov	%l7,%g3
	mov	%sp,%g4
	mov %l0,%g5
trap_patchme:
	mov	0,%g2						// 0 will become NWINDOWS-1
5:	deccc	%g2
	bne,a	5b
tflush2:
	 save

	mov	%g0,%wim
	andn	%g3,PSR_CWP,%g3
	wr	%g3,PSR_PIL|PSR_ET,%psr
	nop; nop; nop
	mov	1<<1,%wim
	mov	%g4,%sp

	// Call the handler
	call	%g5
	 add	%sp,64,%o0

	// fallthrough to env_pop_tf, which is right below this function
	add	%sp,64,%o0

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

	ld	[%o0+152],%l5
	mov	%l5,%asr13
	FINISH_POP_TF(%o0)

	// env_save_tf has a non-standard calling convention.
	// o0: destination trapframe_t*
	// l5: PC
	// l6: nPC
	// l7: PSR
	.global env_save_tf
env_save_tf:

	mov	%psr,%o4
	st	%l7,[%o0+128]
	st	%l5,[%o0+132]
	st	%l6,[%o0+136]
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

	# try to read out the faulting insn (in no-fault mode)
	andn	%l5,3,%o1
	lda	[%g0] 4,%o2
	or	%o2,2,%o3
	sta	%o3,[%g0] 4
	mov	-1,%o3
	ld	[%o1],%o3
	st	%o3,[%o0+156]
	sta	%o2,[%g0] 4
	mov	0x300,%o4
	lda	[%o4] 4,%g0

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
