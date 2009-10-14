#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <atomic.h>
#include <pmap.h>
#include <arch/frontend.h>

volatile int magic_mem[8] __attribute__((aligned(32)));

int32_t frontend_syscall_from_user(env_t* p, int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
	int32_t ret;
	#define KBUFSIZE 1024
	char buf[KBUFSIZE];

	switch(syscall_num)
	{
		case RAMP_SYSCALL_write:
			arg2 = arg2 > KBUFSIZE ? KBUFSIZE : arg2;
			if(memcpy_from_user(p,buf,(void*)arg1,arg2))
				return -1;
			arg1 = PADDR((uint32_t)buf);
			ret = frontend_syscall(syscall_num,arg0,arg1,arg2);
			break;

		case RAMP_SYSCALL_open:
			if(memcpy_from_user(p,buf,(void*)arg0,KBUFSIZE))
				return -1;
			arg0 = PADDR((uint32_t)buf);
			ret = frontend_syscall(syscall_num,arg0,arg1,arg2);
			break;

		case RAMP_SYSCALL_fstat:
			ret = frontend_syscall(syscall_num,arg0,PADDR((uint32_t)buf),arg2);
			if(memcpy_to_user(p,(void*)arg1,buf,64))
				return -1;
			break;

		case RAMP_SYSCALL_read:
			arg2 = arg2 > KBUFSIZE ? KBUFSIZE : arg2;
			ret = frontend_syscall(syscall_num,arg0,PADDR((uint32_t)buf),arg2);
			if(memcpy_to_user(p,(void*)arg1,buf,arg2))
				return -1;
			break;

		case RAMP_SYSCALL_getch:
			return frontend_syscall(RAMP_SYSCALL_getch,0,0,0);

		default:
			ret = -1;
			break;
	}

	return ret;
}

int32_t frontend_syscall(int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
	static spinlock_t lock = 0;
	int32_t ret;

	// only one frontend request at a time.
	// interrupts could try to do frontend requests,
	// which would deadlock, so disable them
	spin_lock_irqsave(&lock);

	// write syscall into magic memory
	magic_mem[7] = 0;
	magic_mem[1] = syscall_num;
	magic_mem[2] = arg0;
	magic_mem[3] = arg1;
	magic_mem[4] = arg2;
	magic_mem[0] = 0x80;

	// wait for front-end response
	while(magic_mem[7] == 0)
		;

	ret = magic_mem[1];

	spin_unlock_irqsave(&lock);

	return ret;
}
