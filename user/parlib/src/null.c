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

error_t null_async(async_desc_t** desc)
{
	error_t e;
	syscall_desc_t* sysdesc;
	if (e = get_all_desc(desc, &sysdesc))
		return e;
	return sys_null_async(sysdesc);
}

void cache_buster(uint32_t num_writes, uint32_t val)
{
	sys_cache_buster(num_writes, val);
}

error_t cache_buster_async(async_desc_t** desc, uint32_t num_writes, uint32_t val)
{
	error_t e;
	syscall_desc_t* sysdesc;
	if (e = get_all_desc(desc, &sysdesc))
		return e;
	return sys_cache_buster_async(sysdesc, num_writes, val);
}
