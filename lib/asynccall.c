#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/queue.h>

async_desc_t* get_async_desc(void)
{
	return POOL_GET(&async_desc_pool);
}

error_t waiton_async_call(async_desc_t* desc)
{
	syscall_rsp_t rsp;
	while (!(LIST_EMPTY(desc))) {
		waiton_syscall(LIST_FIRST(desc), &rsp);
		// consider processing the retval out of rsp
		LIST_REMOVE(LIST_FIRST(desc), next);
	}
	return 0;
}

syscall_desc_t* get_sys_desc(async_desc_t* desc)
{
	syscall_desc_t* d = POOL_GET(&syscall_desc_pool);
	if (d)
    	LIST_INSERT_TAIL(desc, d, next);
	return d;
}
