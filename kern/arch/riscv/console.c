#include <arch/console.h>
#include <console.h>
#include <pmap.h>
#include <atomic.h>
#include <smp.h>
#include <kmalloc.h>
#include <monitor.h>
#include <process.h>

struct magic_mem {
	volatile uint64_t words[8];
};
struct fesvr_syscall {
	struct magic_mem magic_mem;
	STAILQ_ENTRY(fesvr_syscall) link;
};
STAILQ_HEAD(fesvr_syscall_tailq, fesvr_syscall);

spinlock_t fesvr_lock = SPINLOCK_INITIALIZER;
struct fesvr_syscall_tailq fesvr_queue;
struct magic_mem fesvr_current __attribute__((aligned(64)));

bool fesvr_busy()
{
	if (mfpcr(PCR_TOHOST))
	{
		assert(core_id() == 0);
		return true;
	}
	
	volatile uint64_t* mm = fesvr_current.words;
	if (mfpcr(PCR_FROMHOST) && mm[6])
	{
		void (*func)(void*, uint64_t*) = (void*)(uintptr_t)mm[6];
		void* farg = (void*)(uintptr_t)mm[7];
		func(farg, (uint64_t*)mm);
	}
	mtpcr(PCR_FROMHOST, 0);

	return false;
}

void fesvr_syscall(long n, long a0, long a1, long a2, long a3,
                   void (*continuation)(void*, uint64_t*), void* arg)
{
	struct fesvr_syscall* mm = kmalloc(sizeof(struct fesvr_syscall), 0);
	assert(mm);

	mm->magic_mem.words[0] = n;
	mm->magic_mem.words[1] = a0;
	mm->magic_mem.words[2] = a1;
	mm->magic_mem.words[3] = a2;
	mm->magic_mem.words[4] = a3;
	mm->magic_mem.words[6] = (uintptr_t)continuation;
	mm->magic_mem.words[7] = (uintptr_t)arg;

	spin_lock_irqsave(&fesvr_lock);
	STAILQ_INSERT_TAIL(&fesvr_queue, mm, link);
	spin_unlock_irqsave(&fesvr_lock);
}

long fesvr_syscall_sync(long n, long a0, long a1, long a2, long a3)
{
	uintptr_t irq_state = disable_irq();
	while (fesvr_busy());

	struct magic_mem mm __attribute__((aligned(64)));
	mm.words[0] = n;
	mm.words[1] = a0;
	mm.words[2] = a1;
	mm.words[3] = a2;
	mm.words[4] = a3;

	mb();
	mtpcr(PCR_TOHOST, PADDR(&mm));
	while (mfpcr(PCR_FROMHOST) == 0);
	mtpcr(PCR_FROMHOST, 0);

	restore_irq(irq_state);
	return mm.words[0];
}

void fesvr_die()
{
	fesvr_syscall_sync(FESVR_SYS_exit, 0, 0, 0, 0);
}

// emulate keyboard input with an alarm
void keyboard_alarm_init()
{
	void cons_poll(struct alarm_waiter* awaiter)
	{
		static bool cons_polling;
		static uint64_t cons_last_polled;
		void cont(void* null, uint64_t* magic_mem)
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
			cons_polling = false;
			cons_last_polled = read_tsc();
		}

#ifdef __CONFIG_DEMO_SLAVE__
		if (!fesvr_busy() && STAILQ_EMPTY(&fesvr_queue) && hashtable_count(pid_hash) == 0)
		{
			uint32_t demo_size = 0, demo_pos = 0;
			assert(sizeof(demo_size) == fesvr_syscall_sync(FESVR_SYS_read, 0, PADDR(&demo_size), sizeof(demo_size), 0));
			void* demo = kmalloc(demo_size, 0);
			assert(demo_size == fesvr_syscall_sync(FESVR_SYS_read, 0, PADDR(demo), demo_size, 0));
			struct file* f = do_file_open("/bin/demo", O_CREAT, O_WRONLY);
			assert(f);
			off_t off = 0;
			assert(demo_size == f->f_op->write(f, demo, demo_size, &off));
			kref_put(&f->f_kref);
			/* this is potentially dangerous.  the compiler will put run_demo on
			 * the stack if it references any other stack variables.  the
			 * compiler might be allowed to do so otherwise too. */
			void run_demo()
			{
				char *argv[2] = {"", "demo"};
				mon_bin_run(2, argv, 0);
			}
			send_kernel_message(core_id(), run_demo, 0, 0, 0, KMSG_ROUTINE);
		}
#else
		if (!cons_polling && read_tsc() - cons_last_polled >= 100)
		{
			cons_polling = true;
			static char buf[64] __attribute__((aligned(64)));
			fesvr_syscall(FESVR_SYS_read_noncanonical, 0, PADDR(buf), sizeof(buf), 0, cont, 0);
		}
#endif

		uint64_t usec = 100;
		if (!fesvr_busy())
		{
			spin_lock(&fesvr_lock);
			if (!STAILQ_EMPTY(&fesvr_queue))
			{
				usec = 10;
				struct fesvr_syscall* s = STAILQ_FIRST(&fesvr_queue);
				fesvr_current = s->magic_mem;
				STAILQ_REMOVE_HEAD(&fesvr_queue, link);
				kfree(s);
				mb();
				mtpcr(PCR_TOHOST, PADDR(&fesvr_current));
			}
			spin_unlock(&fesvr_lock);
		}
		else
			usec = 10;

		set_awaiter_rel(awaiter, usec);
		set_alarm(&per_cpu_info[core_id()].tchain, awaiter);
	}

	STAILQ_INIT(&fesvr_queue);

	static struct alarm_waiter awaiter;
	init_awaiter(&awaiter, cons_poll);
	set_awaiter_rel(&awaiter, 1);
	set_alarm(&per_cpu_info[core_id()].tchain, &awaiter);
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
	extern int booting;
	if (booting)
	{
		fesvr_syscall_sync(FESVR_SYS_write, 1, PADDR(str), len, 0);
		return;
	}

	void cont(void* buf, uint64_t* mm)
	{
		kfree(buf);
	}

	char* buf = kmalloc(len, 0);
	assert(buf);
	memcpy(buf, str, len);
	fesvr_syscall(FESVR_SYS_write, 1, PADDR(buf), len, 0, cont, buf);
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
