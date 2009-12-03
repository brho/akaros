/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <smp.h>
#include <arch/init.h>

size_t argc;
char** argv;
// dummy values to avoid putting this in the BSS section
void* __args[(4096+8)/sizeof(void*)] __attribute__((aligned(8))) = {(void*)1};

static void
init_argc_argv()
{
        argc = (size_t)__args[2];
        argv = (char**)&__args[3];

        //argv[0] should be "none" for ROS.  we'll ignore it.
        if(argc > 0)
        {
                argc--;
                argv++;
        }

        for(size_t i = 0; i < argc; i++)
                argv[i] += (char*)(&__args[2])-(char*)NULL;
}

void arch_init()
{		
	// this returns when all other cores are done and ready to receive IPIs
	init_argc_argv();
	smp_boot();
	proc_init();
}
