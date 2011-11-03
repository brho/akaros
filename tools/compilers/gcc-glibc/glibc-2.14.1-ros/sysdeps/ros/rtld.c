#define _dl_start _dl_start_real
#include <elf/rtld.c>
#undef _dl_start

#include <ros/syscall.h>
#include <ros/procinfo.h>

static ElfW(Addr) __attribute_used__ internal_function
_dl_start(void* arg0)
{
	int argc = 0;
	while(__procinfo.argp[argc])
		argc++;

	char** arg = (char**)alloca((PROCINFO_MAX_ARGP+1)*sizeof(char*));
	arg[0] = (char*)argc;
	memcpy(arg+1,__procinfo.argp,PROCINFO_MAX_ARGP*sizeof(char*));

	return _dl_start_real(arg);
}
