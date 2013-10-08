/* Copyright (c) 2010-13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel threading.  These are for blocking within the kernel for whatever
 * reason, usually during blocking IO operations.  Check out
 * Documentation/kthreads.txt for more info than you care about. */

#ifndef ROS_KERN_KTHREAD_H
#define ROS_KERN_KTHREAD_H

#include <ros/common.h>
#include <trap.h>
#include <sys/queue.h>
#include <atomic.h>

struct proc;
struct kthread;
struct semaphore;
struct semaphore_entry;
TAILQ_HEAD(kthread_tailq, kthread);
LIST_HEAD(semaphore_list, semaphore_entry);


/* This captures the essence of a kernel context that we want to suspend.  When
 * a kthread is running, we make sure its stacktop is the default kernel stack,
 * meaning it will receive the interrupts from userspace. */
struct kthread {
	struct kernel_ctx			context;
	uintptr_t					stacktop;
	struct proc					*proc;
	struct syscall				*sysc;
	void						*errbuf;	/* TODO: avoiding include loops */
	TAILQ_ENTRY(kthread)		link;
	/* ID, other shit, etc */
	bool						is_ktask;	/* default is FALSE */
	char						*name;
};

/* Semaphore for kthreads to sleep on.  0 or less means you need to sleep */
struct semaphore {
	struct kthread_tailq		waiters;
	int 						nr_signals;
	spinlock_t 					lock;
	bool						irq_okay;
};

struct cond_var {
	struct semaphore			sem;
	spinlock_t 					*lock;		/* usually points to internal_ */
	spinlock_t 					internal_lock;
	unsigned long				nr_waiters;
	bool						irq_okay;
};

/* TODO: consider building this into struct semaphore */
struct semaphore_entry {
	struct semaphore sem;
	int fd;
	LIST_ENTRY(semaphore_entry) link;
};

uintptr_t get_kstack(void);
void put_kstack(uintptr_t stacktop);
uintptr_t *kstack_bottom_addr(uintptr_t stacktop);
void kthread_init(void);
struct kthread *__kthread_zalloc(void);
void restart_kthread(struct kthread *kthread);
void kthread_runnable(struct kthread *kthread);
void kthread_yield(void);
void ktask(char *name, void (*fn)(void*), void *arg);
/* Debugging */
void check_poison(char *msg);

void sem_init(struct semaphore *sem, int signals);
void sem_init_irqsave(struct semaphore *sem, int signals);
bool sem_trydown(struct semaphore *sem);
void sem_down(struct semaphore *sem);
bool sem_up(struct semaphore *sem);
bool sem_trydown_irqsave(struct semaphore *sem, int8_t *irq_state);
void sem_down_irqsave(struct semaphore *sem, int8_t *irq_state);
bool sem_up_irqsave(struct semaphore *sem, int8_t *irq_state);

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

#endif /* ROS_KERN_KTHREAD_H */
