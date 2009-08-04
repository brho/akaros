#include <atomic.h>

#ifdef __DEPUTY__
#pragma nodeputy
#endif

volatile int magic_mem[16];

int32_t frontend_syscall(int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
	static spinlock_t lock = 0;
	int32_t ret;

	// only one frontend request at a time.
	// interrupts could try to do frontend requests,
	// which would deadlock, so disable them
	spin_lock_irqsave(&lock);

	// write syscall into magic memory
	magic_mem[1] = 0;
	magic_mem[2] = (uintptr_t)magic_mem;
	magic_mem[3] = syscall_num;
	magic_mem[4] = arg0;
	magic_mem[5] = arg1;
	magic_mem[6] = arg2;
	magic_mem[0] = 0x80;

	// wait for front-end response
	while(magic_mem[1] == 0)
		;

	magic_mem[0] = 0;

	// wait for front-end ack
	while(magic_mem[1] == 1)
		;

	ret = magic_mem[7];

	spin_unlock_irqsave(&lock);

	return ret;
}
