#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/queue.h>

async_desc_t* get_async_desc(void)
{
	async_desc_t* desc = POOL_GET(&async_desc_pool);
	if (desc)
		// Clear out any data that was in the old desc
		memset(desc, 0, sizeof(*desc));
	return desc;
}

error_t waiton_async_call(async_desc_t* desc)
{
	syscall_rsp_t rsp;
	syscall_desc_t* d;
	while (!(LIST_EMPTY(&desc->syslist))) {
		d = LIST_FIRST(&desc->syslist);
		waiton_syscall(d, &rsp);
		// consider processing the retval out of rsp here (TODO)
		// remove from the list and free the syscall desc
		LIST_REMOVE(d, next);
		POOL_PUT(&syscall_desc_pool, d);
	}
	// run a cleanup function for this desc, if available
	if (desc->cleanup)
		desc->cleanup(desc->data);
	// free the asynccall desc
	POOL_PUT(&async_desc_pool, desc);
	return 0;
}

syscall_desc_t* get_sys_desc(async_desc_t* desc)
{
	syscall_desc_t* d = POOL_GET(&syscall_desc_pool);
	if (d) {
		// Clear out any data that was in the old desc
		memset(d, 0, sizeof(*d));
    	LIST_INSERT_TAIL(&desc->syslist, d, next);
	}
	return d;
}
