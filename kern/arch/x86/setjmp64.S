# Kernel implementations for setjmp/longjmp.
#
# int setjmp(struct jmpbuf *env);
# void longjmp(struct jmpbuf *env, int val);

# The jmpbuf struct is defined as below:
# struct jmpbuf {
# 	uintptr_t retaddr; // return address
# 	uintreg_t rsp;     // post-return rsp
# 	uintreg_t rbx;
# 	uintreg_t rbp;
# 	uintreg_t r12;
# 	uintreg_t r13;
# 	uintreg_t r14;
# 	uintreg_t r15;
# };

.text
.align 4
.globl setjmp
.type setjmp, @function
setjmp:
	xorl %eax,%eax     # Zero out the return value for our first return
	pop  %rsi          # Temporarily grab the return address and adjust %rsp
	movq %rsi,(%rdi)   # Save the return address
	movq %rsp,8(%rdi)  # The adjusted %rsp is the post-return %rsp (see longjmp)
	movq %rbx,16(%rdi) # Save all the callee saved registers into the jmpbuf
	movq %rbp,24(%rdi) 
	movq %r12,32(%rdi) 
	movq %r13,40(%rdi) 
	movq %r14,48(%rdi) 
	movq %r15,56(%rdi) 
	push %rsi          # Restore stuff to make the call/return stack happy
	ret
	
.size setjmp,.-setjmp

.text
.align 4
.globl longjmp
.type longjmp, @function
longjmp:
	movl %esi,%eax     # Set the return value to val (32-bit int)
	movq 56(%rdi),%r15 # Restore all the callee saved registers from the jmpbuf
	movq 48(%rdi),%r14  
	movq 40(%rdi),%r13
	movq 32(%rdi),%r12
	movq 24(%rdi),%rbp
	movq 16(%rdi),%rbx
	movq 8(%rdi),%rsp  # Set the post-return %rsp
	jmp *(%rdi)        # Jump back to setjmp callsite (no ret necessary)
	
.size longjmp,.-longjmp
