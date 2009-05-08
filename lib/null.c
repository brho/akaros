// Simple implementation of cprintf console output for the kernel,
// based on printfmt() and the kernel console's cputchar().

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/types.h>
#include <inc/syscall.h>
#include <inc/lib.h>

void null()
{
	sys_null();
}

void null_async(async_desc_t** desc)
{
	current_async_desc = get_async_desc();
	*desc = current_async_desc;
	sys_null_async(get_sys_desc(current_async_desc));
}
