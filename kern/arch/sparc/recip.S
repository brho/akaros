// Very convoluted FP sqrt/div emulation code.
// Newton-Raphson method.

#include <arch/trap_table.h>
#include <arch/arch.h>
#include <ros/memlayout.h>

#define SET_RDD(src)		 \
	srl	%l0,25,%l3	;\
	and	%l3,0x1E,%l3	;\
	sll	%l3,2,%l3	;\
	add	%l6,%l3,%l3	;\
	std	src,[%l3+0x80]

#define GET_RS1D(dest)		 \
	srl	%l0,14,%l3	;\
	and	%l3,0x1E,%l3	;\
	sll	%l3,2,%l3	;\
	add	%l6,%l3,%l3	;\
	WEIRDD_ADDR(%l3+0x80)	;\
	ldd	[%l3+0x80],dest

#define GET_RS2D(dest)		 \
	and	%l0,0x1E,%l3	;\
	sll	%l3,2,%l3	;\
	add	%l6,%l3,%l3	;\
	WEIRDD_ADDR(%l3+0x80)	;\
	ldd	[%l3+0x80],dest

#define SET_RD(src)		 \
	srl	%l0,25,%l3	;\
	and	%l3,0x1F,%l3	;\
	sll	%l3,2,%l3	;\
	add	%l6,%l3,%l3	;\
	st	src,[%l3+0x80]

#define GET_RS1(dest)		 \
	srl	%l0,14,%l3	;\
	and	%l3,0x1F,%l3	;\
	sll	%l3,2,%l3	;\
	add	%l6,%l3,%l3	;\
	WEIRD_ADDR(%l3+0x80)	;\
	ld	[%l3+0x80],dest

#define GET_RS2(dest)		 \
	and	%l0,0x1F,%l3	;\
	sll	%l3,2,%l3	;\
	add	%l6,%l3,%l3	;\
	WEIRD_ADDR(%l3+0x80)	;\
	ld	[%l3+0x80],dest

#define RESTORE_STATE		 \
	ld	[%l6+64],%l1	;\
	ld	[%l6+68],%l2	;\
	ld	[%l6+72],%l3	;\
	ld	[%l6+76],%fsr	;\
	mov	%l3,%psr	;\

#define SAVE_FP_REGS		 \
	std	%f0, [%l6+0x80]	;\
	std	%f2, [%l6+0x88]	;\
	std	%f4, [%l6+0x90]	;\
	std	%f6, [%l6+0x98]	;\
	std	%f8, [%l6+0xA0]	;\
	std	%f10,[%l6+0xA8]	;\
	std	%f12,[%l6+0xB0]	;\
	std	%f14,[%l6+0xB8]	;\
	std	%f16,[%l6+0xC0]	;\
	std	%f18,[%l6+0xC8]	;\
	std	%f20,[%l6+0xD0]	;\
	std	%f22,[%l6+0xD8]	;\
	std	%f24,[%l6+0xE0]	;\
	std	%f26,[%l6+0xE8]	;\
	std	%f28,[%l6+0xF0]	;\
	std	%f30,[%l6+0xF8]

#define RESTORE_FP_REGS		 \
	ldd	[%l6+0x80],%f0	;\
	ldd	[%l6+0x88],%f2	;\
	ldd	[%l6+0x90],%f4	;\
	ldd	[%l6+0x98],%f6	;\
	ldd	[%l6+0xA0],%f8	;\
	ldd	[%l6+0xA8],%f10	;\
	ldd	[%l6+0xB0],%f12	;\
	ldd	[%l6+0xB8],%f14	;\
	ldd	[%l6+0xC0],%f16	;\
	ldd	[%l6+0xC8],%f18	;\
	ldd	[%l6+0xD0],%f20	;\
	ldd	[%l6+0xD8],%f22	;\
	ldd	[%l6+0xE0],%f24	;\
	ldd	[%l6+0xE8],%f26	;\
	ldd	[%l6+0xF0],%f28	;\
	ldd	[%l6+0xF8],%f30

// under odd circumstances, emulate it
#define WEIRDD(reg)		 \
	std	reg,[%l6+112]	;\
	WEIRDD_ADDR(%l6+112)

#define WEIRDD_ADDR(addr)	 \
	ld	[addr],%l1	;\
	sll	%l1,1,%l1	;\
	srl	%l1,21,%l1	;\
	cmp	%l1,0x7FF	;\
	be	giveup		;\
	tst	%l1		;\
	be	giveup

#define WEIRD_ADDR(addr)	 \
	ld	[addr],%l1	;\
	sll	%l1,1,%l1	;\
	srl	%l1,24,%l1	;\
	cmp	%l1,0xFF	;\
	be	giveup		;\
	tst	%l1		;\
	be	giveup

.global fast_fp_exception
fast_fp_exception:

	// get the instruction (no fault mode)
	lda	[%g0] 4, %l3
	or	%l3,2,%l0
	sta	%l0, [%g0] 4
	ld	[%l1], %l0
	sta	%l3, [%g0] 4

	set	bootstacktop-256,%l6
	mov	CORE_ID_REG,%l7
	sll	%l7,KSTKSHIFT,%l7
	sub	%l6,%l7,%l6

	mov	%psr,%l3
	st	%l1,[%l6+64]
	st	%l2,[%l6+68]
	st	%l3,[%l6+72]
	st	%fsr,[%l6+76]

	// decode the instruction
	set	0x81F83FE0, %l3		! opcode mask
	and	%l3,%l0,%l3		!
	set	0x81A009A0, %l4		! fdivs?
	set	0x81A009C0, %l5		! fdivd?
	set	0x81A00520, %l1		! fsqrts?
	set	0x81A00540, %l7		! fsqrtd?
	cmp	%l3,%l4
	be	do_fdivs
	cmp	%l3,%l5
	be	do_fdivd
	cmp	%l3,%l1
	be	do_fsqrts
	cmp	%l3,%l7
	be	do_fsqrtd

	b,a	getout
	// nothing we can handle fast; call fp_exception
giveup:
	RESTORE_FP_REGS
getout:
	RESTORE_STATE
	TRAP_TABLE_ENTRY(fp_exception)

do_fdivs:
	SAVE_FP_REGS
	st	%g0,[%l6+124]
	ld	[%l6+124],%fsr
	GET_RS2(%f0)
	fstod	%f0,%f0
	sethi	%hi(recip_asm),%l1
	jmpl	%l1+%lo(recip_asm),%l7
	 nop
	GET_RS1(%f2)
	fstod	%f2,%f2
	fmuld	%f2,%f0,%f0
	WEIRDD(%f0)
	fdtos	%f0,%f0
	SET_RD(%f0)
	RESTORE_FP_REGS
	RESTORE_STATE
	jmp	%l2
	 rett	%l2+4

do_fdivd:
	SAVE_FP_REGS
	st	%g0,[%l6+124]
	ld	[%l6+124],%fsr
	GET_RS2D(%f0)
	sethi	%hi(recip_asm),%l1
	jmpl	%l1+%lo(recip_asm),%l7
	 nop
	GET_RS1D(%f2)
	fmuld	%f2,%f0,%f0
	WEIRDD(%f0)
	SET_RDD(%f0)
	RESTORE_FP_REGS
	RESTORE_STATE
	jmp	%l2
	 rett	%l2+4

do_fsqrts:
	SAVE_FP_REGS
	st	%g0,[%l6+124]
	ld	[%l6+124],%fsr
	GET_RS2(%f0)
	fstod	%f0,%f0
	sethi	%hi(recip_sqrt_asm),%l1
	jmpl	%l1+%lo(recip_sqrt_asm),%l7
	 nop
	GET_RS2(%f2)
	fstod	%f2,%f2
	fmuld	%f2,%f0,%f0
	WEIRDD(%f0)
	fdtos	%f0,%f0
	SET_RD(%f0)
	RESTORE_FP_REGS
	RESTORE_STATE
	jmp	%l2
	 rett	%l2+4

do_fsqrtd:
	SAVE_FP_REGS
	st	%g0,[%l6+124]
	ld	[%l6+124],%fsr
	GET_RS2D(%f0)
	sethi	%hi(recip_sqrt_asm),%l1
	jmpl	%l1+%lo(recip_sqrt_asm),%l7
	 nop
	GET_RS2D(%f2)
	fmuld	%f2,%f0,%f0
	WEIRDD(%f0)
	SET_RDD(%f0)
	RESTORE_FP_REGS
	RESTORE_STATE
	jmp	%l2
	 rett	%l2+4


.align 8
divlut:
.double 1.0,0.8,0.666666,0.571428

sqrtlut:
.double 1.414214,1.264911,1.154701,1.069045
.double 1.0,0.894427,0.816497,0.755929

zero:	.double 0.0
nzero:	.double -0.0
half:	.double 0.5
two:	.double 2.0
three:	.double 3.0
nan:    .double nan
infty:	.double inf
ninfty:	.double -inf

recip_asm:
	std	%f0,[%l6+120]	! stow input
	ld	[%l6+120],%l3	! l3 = MSW
	srl	%l3,20,%l4	! l4 = {sign,exp}
	andn	%l4,0x800,%l5	! l5 = exp
!	tst	%l5		! denorm?
!	be	recip_asm_denorm!
!	cmp	%l5,0x7FF	! inf?
!	be	recip_asm_inf	!
	sub	%l5,2046,%l5	!
	neg	%l5		! l5 = -exp
	and	%l4,0x800,%l2	! l2 = sign
	or	%l5,%l2,%l4	! l4 = {sign,-exp}
	sll	%l4,20,%l4	! l4 = MSW
	st	%l4,[%l6+112]	! 
	st	%g0,[%l6+116]	! sp+112 = the exponent of the approx
	srl	%l3,15,%l3	!
	and	%l3,0x18,%l3	! l3 = offset into LUT
	set	divlut,%l4	!
	ldd	[%l3+%l4],%f0	! f0 = mantissa of approx
	ldd	[%l6+112],%f2	! f2 = exponent of approx
	fmuld	%f0,%f2,%f0	! f0 = approx
	ldd	[%l6+120],%f2	! f2 = b
	sethi	%hi(two),%l3	! f4 = two
	ldd	[%l3+%lo(two)],%f4

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fsubd	%f4,%f6,%f6	! f6 = 2-x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(2-x*b)	

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fsubd	%f4,%f6,%f6	! f6 = 2-x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(2-x*b)	

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fsubd	%f4,%f6,%f6	! f6 = 2-x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(2-x*b)	

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fsubd	%f4,%f6,%f6	! f6 = 2-x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(2-x*b)	

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fsubd	%f4,%f6,%f6	! f6 = 2-x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(2-x*b)	

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fsubd	%f4,%f6,%f6	! f6 = 2-x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(2-x*b)	
recip_asm_out:
	jmp	%l7+8
	 nop

recip_asm_inf:
	b	giveup
	cmp	%l3,-1		! nan?
	sethi	%hi(nan),%l2
	be	recip_asm_out	! if so, result = nan
	 ldd	[%l2+%lo(nan)],%f0
	cmp	%l4,0x000	! positive inf?
	sethi	%hi(zero),%l2	!
	be	recip_asm_out	! if so, result = zero
	 ldd	[%l2+%lo(zero)],%f0
	sethi	%hi(nzero),%l2	! else, result = -zero
	ldd	[%l2+%lo(nzero)],%f0
	b,a	recip_asm_out

recip_asm_denorm:
	b	giveup
	cmp	%l4,0x000	! positive denorm?
	sethi	%hi(infty),%l2	!
	be	recip_asm_out	! if so, result = inf
	 ldd	[%l2+%lo(infty)],%f0
	sethi	%hi(ninfty),%l2	! else, result = -inf
	ldd	[%l2+%lo(ninfty)],%f0
	b,a	recip_asm_out
	
recip_sqrt_asm:
	std	%f0,[%l6+120]	! stow input
	ld	[%l6+120],%l3	! l3 = MSW
	srl	%l3,20,%l4	! l4 = {sign,exp}
	andn	%l4,0x800,%l5	! l5 = exp
!	tst	%l5		! denorm?
!	be	recip_sqrt_asm_denorm
!	cmp	%l5,0x7FF	! inf?
!	be	recip_sqrt_asm_inf	!
	btst	0x800,%l4	! negative?
	sethi	%hi(nan),%l2	!
	bne	recip_sqrt_asm_out	! if so, result = NaN
	 ldd	[%l2+%lo(nan)],%f0
	sub	%l5,3069,%l5	!
	neg	%l5		! l5 = -exp
	srl	%l5,1,%l5	! l5 = -exp/2
	sll	%l5,20,%l4	! l4 = MSW (sign guaranteed to be 0)
	st	%l4,[%l6+112]	! 
	st	%g0,[%l6+116]	! sp+112 = the exponent of the approx
	srl	%l3,15,%l3	!
	and	%l3,0x38,%l3	! l3 = offset into LUT
	set	sqrtlut,%l4	!
	ldd	[%l3+%l4],%f0	! f0 = mantissa of approx
	ldd	[%l6+112],%f2	! f2 = exponent of approx
	fmuld	%f0,%f2,%f0	! f0 = approx
	ldd	[%l6+120],%f2	! f2 = b
	sethi	%hi(three),%l3	! f4 = three
	ldd	[%l3+%lo(three)],%f4
	sethi	%hi(half),%l3	! f8 = half
	ldd	[%l3+%lo(half)],%f8

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fmuld	%f0,%f6,%f6	! f6 = x*x*b
	fsubd	%f4,%f6,%f6	! f6 = 3-x*x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(3-x*x*b)
	fmuld	%f0,%f8,%f0	! f0 = 0.5*x*(3-x*x*b)

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fmuld	%f0,%f6,%f6	! f6 = x*x*b
	fsubd	%f4,%f6,%f6	! f6 = 3-x*x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(3-x*x*b)
	fmuld	%f0,%f8,%f0	! f0 = 0.5*x*(3-x*x*b)

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fmuld	%f0,%f6,%f6	! f6 = x*x*b
	fsubd	%f4,%f6,%f6	! f6 = 3-x*x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(3-x*x*b)
	fmuld	%f0,%f8,%f0	! f0 = 0.5*x*(3-x*x*b)

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fmuld	%f0,%f6,%f6	! f6 = x*x*b
	fsubd	%f4,%f6,%f6	! f6 = 3-x*x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(3-x*x*b)
	fmuld	%f0,%f8,%f0	! f0 = 0.5*x*(3-x*x*b)

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fmuld	%f0,%f6,%f6	! f6 = x*x*b
	fsubd	%f4,%f6,%f6	! f6 = 3-x*x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(3-x*x*b)
	fmuld	%f0,%f8,%f0	! f0 = 0.5*x*(3-x*x*b)

	fmuld	%f0,%f2,%f6	! f6 = x*b
	fmuld	%f0,%f6,%f6	! f6 = x*x*b
	fsubd	%f4,%f6,%f6	! f6 = 3-x*x*b
	fmuld	%f0,%f6,%f0	! f0 = x*(3-x*x*b)
	fmuld	%f0,%f8,%f0	! f0 = 0.5*x*(3-x*x*b)
recip_sqrt_asm_out:
	jmp	%l7+8
	 nop

recip_sqrt_asm_inf:
	b	giveup
	cmp	%l3,-1		! nan?
	sethi	%hi(nan),%l2
	be	recip_asm_out	! if so, result = nan
	 ldd	[%l2+%lo(nan)],%f0
	cmp	%l4,0x000	! positive inf?
	sethi	%hi(zero),%l2	!
	be	recip_sqrt_asm_out	! if so, result = zero
	 ldd	[%l2+%lo(zero)],%f0
	sethi	%hi(nan),%l2	! else, result = nan
	ldd	[%l2+%lo(nan)],%f0
	b,a	recip_sqrt_asm_out

recip_sqrt_asm_denorm:
	b	giveup
	cmp	%l4,0x000	! positive denorm?
	sethi	%hi(infty),%l2	!
	be	recip_sqrt_asm_out	! if so, result = inf
	 ldd	[%l2+%lo(infty)],%f0
	sethi	%hi(nan),%l2	! else, result = nan
	ldd	[%l2+%lo(nan)],%f0
	b,a	recip_sqrt_asm_out
	
/*

// d2i and i2d are just wrappers to get/set the bits of a float

double recip(double b)
{
  uint64_t i = d2i(b);
  uint64_t i2 = ((2046-((i>>52)&~0x800)) | (i>>52)&0x800) << 52;
  uint64_t i3 = (i >> 50) & 3;
  static const double divlut[4] = {1.0,0.8,0.666,0.571};
  double x = i2d(i2)*divlut[i3];

  x = x*(2.0-b*x);
  x = x*(2.0-b*x);
  x = x*(2.0-b*x);
  x = x*(2.0-b*x);
  x = x*(2.0-b*x);

  return x;
}

double recip_sqrt(double b)
{
  uint64_t i = d2i(b);
  uint64_t i2 = ((3069-((i>>52)&~0x800))>>1 | (i>>52)&0x800) << 52;
  uint64_t i3 = (i >> 50) & 7;
  double x = i2d(i2);

  static const double sqrtlut[8] = {1.4142,1.264,1.155,1.069, 1.0,0.894,0.816,0.756};
  x *= sqrtlut[i3];

  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);
  x = 0.5*x*(3.0-b*x*x);

  return x;
}
*/
