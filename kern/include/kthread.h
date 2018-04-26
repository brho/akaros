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
struct semaphore;
TAILQ_HEAD(kthread_tailq, kthread);
TAILQ_HEAD(semaphore_tailq, semaphore);

#define GENBUF_SZ 128	/* plan9 uses this as a scratch space, per syscall */

#define KTH_IS_KTASK			(1 << 0)
#define KTH_SAVE_ADDR_SPACE		(1 << 1)
#define KTH_KTASK_FLAGS			(KTH_IS_KTASK)
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

/* Semaphore for kthreads to sleep on.  0 or less means you need to sleep */
struct semaphore {
	struct kthread_tailq		waiters;
	int 						nr_signals;
	spinlock_t 					lock;
	bool						irq_okay;
#ifdef CONFIG_SEMAPHORE_DEBUG
	TAILQ_ENTRY(semaphore)		link;
	bool						is_on_list;	/* would like better sys/queue.h */
#endif
};

/* omitted elements (the sem debug stuff) are initialized to 0 */
#define SEMAPHORE_INITIALIZER(name, n)                                         \
{                                                                              \
    .waiters    = TAILQ_HEAD_INITIALIZER((name).waiters),                      \
	.nr_signals = (n),                                                         \
    .lock       = SPINLOCK_INITIALIZER,                                        \
    .irq_okay   = FALSE,                                                       \
}

#define SEMAPHORE_INITIALIZER_IRQSAVE(name, n)                                 \
{                                                                              \
    .waiters    = TAILQ_HEAD_INITIALIZER((name).waiters),                      \
	.nr_signals = (n),                                                         \
    .lock       = SPINLOCK_INITIALIZER_IRQSAVE,                                \
    .irq_okay   = TRUE,                                                        \
}

struct cond_var {
	struct semaphore			sem;
	spinlock_t 					*lock;		/* usually points to internal_ */
	spinlock_t 					internal_lock;
	unsigned long				nr_waiters;
	bool						irq_okay;
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

void sem_init(struct semaphore *sem, int signals);
void sem_init_irqsave(struct semaphore *sem, int signals);
bool sem_trydown_bulk(struct semaphore *sem, int nr_signals);
bool sem_trydown(struct semaphore *sem);
void sem_down_bulk(struct semaphore *sem, int nr_signals);
void sem_down(struct semaphore *sem);
bool sem_up(struct semaphore *sem);
bool sem_trydown_bulk_irqsave(struct semaphore *sem, int nr_signals,
                              int8_t *irq_state);
bool sem_trydown_irqsave(struct semaphore *sem, int8_t *irq_state);
void sem_down_bulk_irqsave(struct semaphore *sem, int nr_signals,
                           int8_t *irq_state);
void sem_down_irqsave(struct semaphore *sem, int8_t *irq_state);
bool sem_up_irqsave(struct semaphore *sem, int8_t *irq_state);
void print_all_sem_info(pid_t pid);

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

bool abort_sysc(struct proc *p, struct syscall *sysc);
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
