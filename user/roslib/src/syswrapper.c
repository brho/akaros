#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <types.h>
#include <lib.h>
#include <ros/syscall.h>

void null()
{
	sys_null();
}

error_t null_async(async_desc_t** desc)
{
	error_t e;
	syscall_desc_t* sysdesc;
	if (e = get_all_desc(desc, &sysdesc))
		return e;
	return sys_null_async(sysdesc);
}

void cache_buster(uint32_t num_writes, uint32_t num_pages, uint32_t flags)
{
	sys_cache_buster(num_writes, num_pages, flags);
}

error_t cache_buster_async(async_desc_t** desc, uint32_t num_writes,
                           uint32_t num_pages, uint32_t flags)
{
	error_t e;
	syscall_desc_t* sysdesc;
	if (e = get_all_desc(desc, &sysdesc))
		return e;
	return sys_cache_buster_async(sysdesc, num_writes, num_pages, flags);
}

uint32_t getcpuid(void)
{
	return sys_getcpuid();
}
