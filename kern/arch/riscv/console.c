#include <arch/console.h>
#include <console.h>
#include <pmap.h>
#include <atomic.h>
#include <smp.h>

static volatile uint64_t magic_mem[MAX_NUM_CPUS][8] __attribute__((aligned(64)));

static bool fesvr_busy()
{
	if (mfpcr(PCR_TOHOST))
		return true;
	
	volatile uint64_t* mm = magic_mem[core_id()];
	if (mfpcr(PCR_FROMHOST) && mm[6])
	{
		void (*func)(void*, uint64_t*) = (void*)(uintptr_t)mm[6];
		void* farg = (void*)(uintptr_t)mm[7];
		func(farg, (uint64_t*)mm);
	}

	return false;
}

int fesvr_syscall(long n, long a0, long a1, long a2, long a3,
                  void (*continuation)(void*, uint64_t*), void* arg)
{
	int ret = -1;
	uintptr_t irq_state = disable_irq();

	if (fesvr_busy())
		goto out;

	volatile uint64_t* mm = magic_mem[core_id()];
	mm[0] = n;
	mm[1] = a0;
	mm[2] = a1;
	mm[3] = a2;
	mm[4] = a3;
	mm[6] = (uintptr_t)continuation;
	mm[7] = (uintptr_t)arg;

	mb();
	mtpcr(PCR_TOHOST, PADDR(mm));
  
	ret = 0;
out:
	restore_irq(irq_state);
	return ret;
}

void
fesvr_die()
{
	while (fesvr_syscall(FESVR_SYS_exit, 0, 0, 0, 0, 0, 0) < 0);
}

static void cons_polled(void* null, uint64_t* magic_mem)
{
	for (int i = 0; i < (int)magic_mem[0]; i++)
	{
		char c = ((char*)KADDR(magic_mem[2]))[i];
		if (c == 'G')
			send_kernel_message(core_id(), __run_mon, 0, 0, 0, KMSG_ROUTINE);
		else
			send_kernel_message(core_id(), __cons_add_char, (long)&cons_buf,
			                    (long)c, 0, KMSG_ROUTINE);
	}
}

static struct alarm_waiter keyboard_waiter;

static void cons_poll(struct alarm_waiter* awaiter)
{
	static char buf[64] __attribute__((aligned(64)));
	fesvr_syscall(FESVR_SYS_read, 0, PADDR(buf), sizeof(buf), 0, cons_polled, 0);

	set_awaiter_rel(&keyboard_waiter, 10);
	set_alarm(&per_cpu_info[core_id()].tchain, &keyboard_waiter);
}

// emulate keyboard input with an alarm
void keyboard_alarm_init()
{
	init_awaiter(&keyboard_waiter, cons_poll);
	set_awaiter_rel(&keyboard_waiter, 1);
	set_alarm(&per_cpu_info[core_id()].tchain, &keyboard_waiter);
}

int cons_get_any_char(void)
{
	assert(0);
}

void
cons_init(void)
{
}

// `High'-level console I/O.  Used by readline and cprintf.

void
cputbuf(const char* str, int len)
{
	static char bufs[MAX_NUM_CPUS][1024] __attribute__((aligned(64)));
	assert(len <= sizeof(bufs[0]));

	char* buf = bufs[core_id()];
	while (fesvr_busy());
	memcpy(buf, str, len);
	while (fesvr_syscall(FESVR_SYS_write, 1, PADDR(buf), len, 0, 0, 0) < 0);
}

// Low-level console I/O

void
cputchar(int c)
{
	char ch = c;
	cputbuf(&ch,1);
}

int
getchar(void)
{
	char c;
	kb_get_from_buf(&cons_buf, &c, 1);
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}
