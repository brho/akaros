#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <atomic.h>
#include <pmap.h>
#include <arch/frontend.h>
#include <smp.h>

volatile int magic_mem[10];

int32_t frontend_syscall_from_user(env_t* p, int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t translate_args)
{
	// really, we just want to pin pages, but irqdisable works
	static spinlock_t lock = SPINLOCK_INITIALIZER;
	spin_lock_irqsave(&lock);

	uint32_t arg[3] = {arg0,arg1,arg2};
	for(int i = 0; i < 3; i++)
	{
		int flags = (translate_args & (1 << (i+3))) ? PTE_USER_RW :
		           ((translate_args & (1 << i)) ? PTE_USER_RO : 0);
		if(flags)
		{
			pte_t* pte = pgdir_walk(p->env_pgdir,(void*)arg[i],0);
			if(pte == NULL || !(*pte & flags))
			{
				spin_unlock_irqsave(&lock);
				return -1;
			}
			arg[i] = PTE_ADDR(*pte) | PGOFF(arg[i]);
		}
	}

	int32_t ret = frontend_syscall(p->pid,syscall_num,arg[0],arg[1],arg[2]);
	spin_unlock_irqsave(&lock);
	return ret;
}

int32_t frontend_syscall(pid_t pid, int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
	static spinlock_t lock = SPINLOCK_INITIALIZER;
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
	magic_mem[6] = pid;
	magic_mem[0] = 0x80;

	// wait for front-end response
	while(magic_mem[7] == 0)
		;

	ret = magic_mem[1];

	spin_unlock_irqsave(&lock);

	return ret;
}

int32_t sys_nbputch(char ch)
{
	static spinlock_t putch_lock = SPINLOCK_INITIALIZER;
	spin_lock_irqsave(&putch_lock);

	int ret = -1;
	if(magic_mem[8] == 0)
	{
		magic_mem[8] = (unsigned int)(unsigned char)ch;
		ret = 0;
	}

	spin_unlock_irqsave(&putch_lock);
	return ret;
}

int32_t sys_nbgetch()
{
	static spinlock_t getch_lock = SPINLOCK_INITIALIZER;
	spin_lock_irqsave(&getch_lock);

	int result = -1;
	if(magic_mem[9]) 
	{
		result = magic_mem[9];
		magic_mem[9] = 0;
	}

	spin_unlock_irqsave(&getch_lock);
	return result;
}

void __diediedie(trapframe_t* tf, uint32_t srcid, uint32_t code, uint32_t a1, uint32_t a2)
{
	frontend_syscall(0,RAMP_SYSCALL_exit,(int)code,0,0);
	while(1);
}

void appserver_die(int code)
{
	int i;
	for(i = 0; i < num_cpus; i++)
		if(i != core_id())
			while(send_active_message(i,(amr_t)&__diediedie,(void*)code,0,0));

	// just in case.
	__diediedie(0,0,code,0,0);
}
