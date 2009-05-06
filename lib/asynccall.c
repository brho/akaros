#include <inc/lib.h>
#include <inc/syscall.h>

// assumes all calls are to the same ring buffer
syscall_desc_t ALL_ASYNC_CALLS[20][20];

error_t waiton_async_call(async_desc desc)
{
	return 0;
}

