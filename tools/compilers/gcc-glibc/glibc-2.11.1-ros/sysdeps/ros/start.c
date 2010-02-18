#include <abort-instr.h>
#include <string.h>
#include <ros/arch/hart.h>
#include <stdio.h>
#include "ros_syscall.h"

void** __hart_stack_pointers = NULL;
weak_alias(__hart_stack_pointers,hart_stack_pointers)
void** __hart_thread_control_blocks = NULL;
weak_alias(__hart_thread_control_blocks,hart_thread_control_blocks)

void
__hart_entry(void)
{
	fputs("define a hart_entry() function, foo!",stderr);
	abort();
}
weak_alias(__hart_entry,hart_entry)

void
__hart_yield(void)
{
}
weak_alias(__hart_yield,hart_yield)

void
_start(void)
{
	// threads besides thread 0 must acquire a stack and TCB
	if(__hart_self() != 0)
	{
		__hart_set_stack_pointer(__hart_stack_pointers[__hart_self()]);
		TLS_INIT_TP(__hart_thread_control_blocks[__hart_self()],0);
		hart_entry();
		hart_yield();
	}

	static int init = 0;
	if(init)
	{
		fputs("why is thread 0 re-entering _start, foo?!",stderr);
		abort();
	}
	init = 1;

	extern int main(int,char**,char**);
	extern void __libc_csu_init(int,char**,char**);
	extern void __libc_csu_fini(void);
	extern void __libc_start_main(typeof(&main),int,char**,
	              typeof(&__libc_csu_init),
	              typeof(&__libc_csu_fini),
	              void*,void*);

	char** argv = (char**)alloca(sizeof(__procinfo.argp));
	memcpy(argv,__procinfo.argp,sizeof(__procinfo.argp));

	char* argbuf = (char*)alloca(sizeof(__procinfo.argbuf));
	memcpy(argbuf,__procinfo.argbuf,sizeof(__procinfo.argbuf));

	for(int i = 0; i < PROCINFO_MAX_ARGP; i++)
		if(argv[i])
			argv[i] += argbuf - __procinfo.argbuf;

	int argc = 0;
	while(argv[argc])
		argc++;

	__libc_start_main(&main,argc,argv,&__libc_csu_init,&__libc_csu_fini,0,0);

	#ifdef ABORT_INSTRUCTION
	ABORT_INSTRUCTION;
	#endif
	while(1);
}

