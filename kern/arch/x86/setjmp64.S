# Kernel implementations for a slim setjmp/longjmp.
#
# int setjmp(struct jmpbuf *env);
# void longjmp(struct jmpbuf *env, int val);
#
# Callers *must* clobber all callee-saved registers first, e.g.:
# 	asm volatile ("" : : : "rbx", "r12", "r13", "r14", "r15");

# The jmpbuf struct is defined as below:
# struct jmpbuf {
# 	uintptr_t retaddr; // return address
# 	uintreg_t rsp;     // post-return rsp
# 	uintreg_t rbp;
# };

.text
.align 4
.globl slim_setjmp
.type slim_setjmp, @function
slim_setjmp:
	xorl %eax,%eax     # Zero out the return value for our first return
	pop  %rsi          # Temporarily grab the return address and adjust %rsp
	movq %rsi,(%rdi)   # Save the return address
	movq %rsp,8(%rdi)  # The adjusted %rsp is the post-return %rsp
			   # (see longjmp)
	movq %rbp,16(%rdi)
	push %rsi          # Restore stuff to make the call/return stack happy
	ret

.size slim_setjmp,.-slim_setjmp

.text
.align 4
.globl longjmp
.type longjmp, @function
longjmp:
	movl %esi,%eax     # Set the return value to val (32-bit int)
	movq 16(%rdi),%rbp
	movq 8(%rdi),%rsp  # Set the post-return %rsp
	jmp *(%rdi)        # Jump back to setjmp callsite (no ret necessary)

.size longjmp,.-longjmp
