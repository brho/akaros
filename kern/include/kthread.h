/* Copyright (c) 2010-13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel threading.  These are for blocking within the kernel for whatever
 * reason, usually during blocking IO operations.  Check out
 * Documentation/kthreads.txt for more info than you care about. */

#pragma once

#include <ros/common.h>
#include <ros/syscall.h>
#include <trap.h>
#include <sys/queue.h>
#include <atomic.h>
#include <setjmp.h>

struct errbuf {
	struct jmpbuf jmpbuf;
};

struct proc;
struct kthread;
struct kth_db_info;
TAILQ_HEAD(kthread_tailq, kthread);
TAILQ_HEAD(kth_db_tailq, kth_db_info);

#define GENBUF_SZ 128	/* plan9 uses this as a scratch space, per syscall */

#define KTH_IS_KTASK			(1 << 0)
#define KTH_SAVE_ADDR_SPACE		(1 << 1)
#define KTH_IS_RCU_KTASK		(1 << 2)

/* These flag sets are for toggling between ktasks and default/process ktasks */
/* These are the flags for *any* ktask */
#define KTH_KTASK_FLAGS			(KTH_IS_KTASK)
/* These are the flags used for normal process context */
#define KTH_DEFAULT_FLAGS		(KTH_SAVE_ADDR_SPACE)

/* This captures the essence of a kernel context that we want to suspend.  When
 * a kthread is running, we make sure its stacktop is the default kernel stack,
 * meaning it will receive the interrupts from userspace. */
struct kthread {
	struct jmpbuf				context;
	uintptr_t					stacktop;
	struct proc					*proc;
	struct syscall				*sysc;
	struct errbuf				*errbuf;
	TAILQ_ENTRY(kthread)		link;
	/* ID, other shit, etc */
	int							flags;
	char						*name;
	char						generic_buf[GENBUF_SZ];
	int							errno;
	char						errstr[MAX_ERRSTR_LEN];
	struct systrace_record		*strace;
};

#define KTH_DB_SEM			1
#define KTH_DB_CV			2

#ifdef CONFIG_SEMAPHORE_DEBUG

struct kth_db_info {
	TAILQ_ENTRY(kth_db_info)	link;
	unsigned int				type;
	bool						on_list;
};

#define KTH_DB_INIT .db         = { .type = KTH_DB_SEM },

#else

struct kth_db_info {
};

#define KTH_DB_INIT

#endif


/* Semaphore for kthreads to sleep on.  0 or less means you need to sleep */
struct semaphore {
	struct kth_db_info			db;
	struct kthread_tailq		waiters;
	int 						nr_signals;
	spinlock_t 					lock;
};

#define SEMAPHORE_INITIALIZER(name, n)                                         \
{                                                                              \
    .waiters    = TAILQ_HEAD_INITIALIZER((name).waiters),                      \
	.nr_signals = (n),                                                         \
    .lock       = SPINLOCK_INITIALIZER,                                        \
	KTH_DB_INIT                                                                \
}

#define SEMAPHORE_INITIALIZER_IRQSAVE(name, n)                                 \
{                                                                              \
    .waiters    = TAILQ_HEAD_INITIALIZER((name).waiters),                      \
	.nr_signals = (n),                                                         \
    .lock       = SPINLOCK_INITIALIZER_IRQSAVE,                                \
	KTH_DB_INIT                                                                \
}

struct cond_var {
	struct kth_db_info			db;
	struct kthread_tailq		waiters;
	spinlock_t 					*lock;		/* usually points to internal_ */
	spinlock_t 					internal_lock;
	unsigned long				nr_waiters;
};

struct cv_lookup_elm {
	TAILQ_ENTRY(cv_lookup_elm)	link;
	TAILQ_ENTRY(cv_lookup_elm)	abortall_link;		/* only used in abort_all */
	struct cond_var				*cv;
	struct kthread				*kthread;
	struct syscall				*sysc;
	struct proc					*proc;
	atomic_t					abort_in_progress;	/* 0 = no */
};
TAILQ_HEAD(cv_lookup_tailq, cv_lookup_elm);

uintptr_t get_kstack(void);
void put_kstack(uintptr_t stacktop);
uintptr_t *kstack_bottom_addr(uintptr_t stacktop);
void kthread_init(void);
struct kthread *__kthread_zalloc(void);
void __use_real_kstack(void (*f)(void *arg));
void restart_kthread(struct kthread *kthread);
void kthread_runnable(struct kthread *kthread);
void kthread_yield(void);
void kthread_usleep(uint64_t usec);
void ktask(char *name, void (*fn)(void*), void *arg);

static inline bool is_ktask(struct kthread *kthread)
{
	return kthread->flags & KTH_IS_KTASK;
}

static inline bool is_rcu_ktask(struct kthread *kthread)
{
	return kthread->flags & KTH_IS_RCU_KTASK;
}

void sem_init(struct semaphore *sem, int signals);
void sem_init_irqsave(struct semaphore *sem, int signals);
bool sem_trydown_bulk(struct semaphore *sem, int nr_signals);
bool sem_trydown(struct semaphore *sem);
void sem_down_bulk(struct semaphore *sem, int nr_signals);
void sem_down(struct semaphore *sem);
bool sem_up(struct semaphore *sem);
bool sem_trydown_bulk_irqsave(struct semaphore *sem, int nr_signals);
bool sem_trydown_irqsave(struct semaphore *sem);
void sem_down_bulk_irqsave(struct semaphore *sem, int nr_signals);
void sem_down_irqsave(struct semaphore *sem);
bool sem_up_irqsave(struct semaphore *sem);
void print_db_blk_info(pid_t pid);

void cv_init(struct cond_var *cv);
void cv_init_irqsave(struct cond_var *cv);
void cv_init_with_lock(struct cond_var *cv, spinlock_t *lock);
void cv_init_irqsave_with_lock(struct cond_var *cv, spinlock_t *lock);
void cv_lock(struct cond_var *cv);
void cv_unlock(struct cond_var *cv);
void cv_lock_irqsave(struct cond_var *cv, int8_t *irq_state);
void cv_unlock_irqsave(struct cond_var *cv, int8_t *irq_state);
void cv_wait_and_unlock(struct cond_var *cv);	/* does not mess with irqs */
void cv_wait(struct cond_var *cv);
void __cv_signal(struct cond_var *cv);
void __cv_broadcast(struct cond_var *cv);
void cv_signal(struct cond_var *cv);
void cv_broadcast(struct cond_var *cv);
void cv_signal_irqsave(struct cond_var *cv, int8_t *irq_state);
void cv_broadcast_irqsave(struct cond_var *cv, int8_t *irq_state);

bool abort_sysc(struct proc *p, uintptr_t sysc);
void abort_all_sysc(struct proc *p);
int abort_all_sysc_fd(struct proc *p, int fd);
void __reg_abortable_cv(struct cv_lookup_elm *cle, struct cond_var *cv);
void dereg_abortable_cv(struct cv_lookup_elm *cle);
bool should_abort(struct cv_lookup_elm *cle);

uintptr_t switch_to_ktask(void);
void switch_back_from_ktask(uintptr_t old_ret);

/* qlocks are plan9's binary sempahore, which are wrappers around our sems.
 * Not sure if they'll need irqsave or normal sems. */
typedef struct semaphore qlock_t;
#define qlock_init(x) sem_init((x), 1)
#define qlock(x) sem_down(x)
#define qunlock(x) sem_up(x)
#define canqlock(x) sem_trydown(x)
#define QLOCK_INITIALIZER(name) SEMAPHORE_INITIALIZER(name, 1)
