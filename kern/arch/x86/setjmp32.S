# Kernel implementations for slim setjmp/longjmp.
#
# int setjmp(struct jmpbuf *env);
# void longjmp(struct jmpbuf *env, int val);

# The jmpbuf struct is defined as below:
# struct jmpbuf {
#	uintptr_t retaddr; // return address
# 	uintreg_t esp;     // post-return esp
#	uintreg_t ebp;
# };

.text
.align 4
.globl slim_setjmp
.type slim_setjmp, @function
slim_setjmp:
	movl 4(%esp),%edx  # Grab a reference to the jmpbuf passed in
	xorl %eax,%eax     # Zero out the return value for our first return
	popl %ecx          # Temporarily grab the return address and adjust %rsp
	movl %ecx,(%edx)   # Save the return address
	movl %esp,4(%edx)  # The adjusted %esp is the post-return %esp (see longjmp)
	movl %ebp,8(%edx)
	pushl %ecx         # Restore stuff to make the call/return stack happy
	ret
 
.size slim_setjmp,.-slim_setjmp
 
.text
.align 4
.globl longjmp
.type longjmp, @function
longjmp:
	movl 4(%esp),%edx  # Grab a reference to the jmpbuf passed in
	movl 8(%esp),%eax  # Set the return value to val (32-bit int)
	movl 8(%edx),%ebp
	movl 4(%edx),%esp  # Set the post-return %rsp
	jmp *(%edx)        # Jump back to setjmp callsite (no ret necessary)
 
.size longjmp,.-longjmp
