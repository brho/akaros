#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <atomic.h>
#include <pmap.h>
#include <arch/frontend.h>

volatile int magic_mem[10];

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

			extern spinlock_t output_lock;
			spin_lock(&output_lock);

			if(arg0 == 1 || arg0 == 2)
			{
				int i;
				for(i = 0; i < arg2; i++)
					cputchar(buf[i]);
				ret = arg2;
			}
			else
				ret = frontend_syscall(syscall_num,arg0,arg1,arg2);

			spin_unlock(&output_lock);

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

			if(arg0 == 0)
			{
				if(arg2 > 0)
				{
					int ch = getchar();
					buf[0] = (char)ch;
					ret = 1;
				}
				else
					ret = 0;
			}
			else
				ret = frontend_syscall(syscall_num,arg0,PADDR((uint32_t)buf),arg2);

			if(memcpy_to_user(p,(void*)arg1,buf,arg2))
				return -1;
			break;

		case RAMP_SYSCALL_getch:
			return frontend_syscall(RAMP_SYSCALL_getch,0,0,0);

		case RAMP_SYSCALL_gettimeofday:
		{
			struct timeval
			{
				size_t tv_sec;
				size_t tv_usec;
			};

			static spinlock_t gettimeofday_lock = 0;
			static size_t t0 = 0;
			spin_lock(&gettimeofday_lock);

			if(!t0)
			{
				volatile struct timeval tp;
				ret = frontend_syscall(RAMP_SYSCALL_gettimeofday,(int)PADDR((uint32_t)&tp),0,0);
				if(ret == 0)
					t0 = tp.tv_sec;
			}
			else ret = 0;

			spin_unlock(&gettimeofday_lock);

			if(ret == 0)
			{
				struct timeval tp;
				long long dt = read_tsc();
				tp.tv_sec = t0 + dt/system_timing.tsc_freq;
				tp.tv_usec = (dt % system_timing.tsc_freq)*1000000/system_timing.tsc_freq;

				ret = memcpy_to_user(p,(void*)arg0,&tp,sizeof(tp));
			}
		}
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

int32_t sys_nbputch(char ch)
{
	static spinlock_t putch_lock = 0;
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
	static spinlock_t getch_lock = 0;
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
