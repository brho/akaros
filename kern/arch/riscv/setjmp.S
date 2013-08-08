# Kernel implementations for setjmp/longjmp.
# TODO: Implement for riscv
#
# int setjmp(struct jmpbuf *env);
# void longjmp(struct jmpbuf *env, int val);

.text
.align 4
.globl setjmp
.type setjmp, @function
setjmp:
	ret
	
.size setjmp,.-setjmp

.text
.align 4
.globl longjmp
.type longjmp, @function
longjmp:
	ret
	
.size longjmp,.-longjmp
