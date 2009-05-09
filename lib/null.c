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
	if ((current_async_desc = get_async_desc()) == NULL)
		return E_BUSY;
	*desc = current_async_desc;
	return sys_null_async(get_sys_desc(current_async_desc));
}
