#include <lib.h>

extern env_t* env;

void exit(void) __attribute__((noreturn))
{
	sys_env_destroy(env->env_id);
	//Shouldn't get here, but include anyway so the compiler is happy.. 
	while(1);
}

