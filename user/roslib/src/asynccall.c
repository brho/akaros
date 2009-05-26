#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <lib.h>
#include <queue.h>
#include <ros/syscall.h>

// Wait on all syscalls within this async call.  TODO - timeout or something?
error_t waiton_async_call(async_desc_t* desc, async_rsp_t* rsp)
{
	syscall_rsp_t syscall_rsp;
	syscall_desc_t* d;
	error_t err = 0;
	if (!desc)
		return -E_INVAL;
	while (!(LIST_EMPTY(&desc->syslist))) {
		d = LIST_FIRST(&desc->syslist);
		err = MIN(waiton_syscall(d, &syscall_rsp), err);
		// TODO: processing the retval out of rsp here.  might be specific to
		// the async call.  do we want to accumulate?  return any negative
		// values?  depends what we want from the return value, so we might
		// have to pass in a function that is used to do the processing and
		// pass the answer back out in rsp.
		//rsp->retval += syscall_rsp.retval; // For example
		rsp->retval = MIN(rsp->retval, syscall_rsp.retval);
		// remove from the list and free the syscall desc
		LIST_REMOVE(d, next);
		POOL_PUT(&syscall_desc_pool, d);
	}
	// run a cleanup function for this desc, if available
	if (desc->cleanup)
		desc->cleanup(desc->data);
	// free the asynccall desc
	POOL_PUT(&async_desc_pool, desc);
	return err;
}

// Finds a free async_desc_t, on which you can wait for a series of syscalls
async_desc_t* get_async_desc(void)
{
	async_desc_t* desc = POOL_GET(&async_desc_pool);
	if (desc)
		// Clear out any data that was in the old desc
		memset(desc, 0, sizeof(*desc));
	return desc;
}

// Finds a free sys_desc_t, on which you can wait for a specific syscall, and
// binds it to the group desc.
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

// Gets an async and a sys desc, with the sys bound to async.  Also sets
// current_async_desc.  This is meant as an easy wrapper when there is only one
// syscall for an async call.
error_t get_all_desc(async_desc_t** a_desc, syscall_desc_t** s_desc)
{
	assert(a_desc && s_desc);
	if ((current_async_desc = get_async_desc()) == NULL)
		return E_BUSY;
	*a_desc = current_async_desc;
	if (*s_desc = get_sys_desc(current_async_desc))
		return 0;
	// in case we could get an async, but not a syscall desc, then clean up.
	POOL_PUT(&async_desc_pool, current_async_desc);
	current_async_desc = NULL;
	return E_BUSY;
}
