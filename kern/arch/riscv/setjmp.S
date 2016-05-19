# Kernel implementations for slim setjmp/longjmp.
# TODO: Implement for riscv
#
# int setjmp(struct jmpbuf *env);
# void longjmp(struct jmpbuf *env, int val);

.text
.align 4
.globl slim_setjmp
.type slim_setjmp, @function
slim_setjmp:
	ret

.size slim_setjmp,.-slim_setjmp

.text
.align 4
.globl longjmp
.type longjmp, @function
longjmp:
	ret

.size longjmp,.-longjmp
