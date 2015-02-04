#include <uthread.h>
#include <semaphore.h>
#include <mcs.h>
#include <stdio.h>
#include <alarm.h>
#include <errno.h>

struct sem_queue_element {
	TAILQ_ENTRY(sem_queue_element) next;
	struct sem *sem;
	pthread_t pthread;
	uint64_t us_timeout;
	struct alarm_waiter awaiter;
	bool timedout;
};

int sem_init (sem_t *__sem, int __pshared, unsigned int __value)
{
	if(__pshared == TRUE) {
		printf("__pshared functionality of sem_init is not yet implemented!");
		return -1;
	}
	__sem->count = __value;
	TAILQ_INIT(&__sem->queue);
	spin_pdr_init(&__sem->lock);
	return 0;
}

int sem_destroy (sem_t *__sem)
{
	return 0;
}

sem_t *sem_open (__const char *__name, int __oflag, ...)
{
	printf("sem_open is not yet implemented!");
	return NULL;
}

int sem_close (sem_t *__sem)
{
	printf("sem_close is not yet implemented!");
	return -1;
}

int sem_unlink (__const char *__name)
{
	printf("sem_unlink is not yet implemented!");
	return -1;
}

static void __sem_timeout(struct alarm_waiter *awaiter)
{
	struct sem_queue_element *e = awaiter->data;
	struct sem_queue_element *__e = NULL;

	/* Try and yank out the thread. */
	spin_pdr_lock(&e->sem->lock);
	TAILQ_FOREACH(__e, &e->sem->queue, next)
		if (__e == e) break;
	if (__e) {
		TAILQ_REMOVE(&e->sem->queue, e, next);
		e->timedout = true;
	}
	spin_pdr_unlock(&e->sem->lock);

	/* If we were able to yank it out, wake it up. */
	if (__e)
		uthread_runnable((struct uthread*)e->pthread);

	/* Set this as the very last thing we do whether we
	 * successfully woke the thread blocked on the futex or not.
	 * Either we set this or post() sets this, not both.  Spin on
	 * this in the bottom-half of the wait() code to ensure there
	 * are no more references to awaiter before freeing the
	 * memory for it. */
	e->awaiter.data = NULL;
}

static void __sem_block(struct uthread *uthread, void *arg)
{
	struct sem_queue_element *e = (struct sem_queue_element *)arg;
	pthread_t pthread = (pthread_t)uthread;
	__pthread_generic_yield(pthread);
	pthread->state = PTH_BLK_MUTEX;
	TAILQ_INSERT_TAIL(&e->sem->queue, e, next);
	spin_pdr_unlock(&e->sem->lock);
}

static void __sem_timedblock(struct uthread *uthread, void *arg)
{
	struct sem_queue_element *e = (struct sem_queue_element *)arg;
	e->awaiter.data = e;
	init_awaiter(&e->awaiter, __sem_timeout);
	set_awaiter_abs(&e->awaiter, e->us_timeout);
	set_alarm(&e->awaiter);
	__sem_block(uthread, e->sem);
}

int sem_wait (sem_t *__sem)
{
	pthread_t pthread = (pthread_t)current_uthread;
	struct sem_queue_element e = {{0}, __sem, pthread, -1, {0}, false};

	spin_pdr_lock(&__sem->lock);
	if(__sem->count > 0) {
		__sem->count--;
		spin_pdr_unlock(&__sem->lock);
	}
	else {
		/* We unlock in the body of __sem_block */
		uthread_yield(TRUE, __sem_block, __sem);
	}
	return 0;
}

int sem_timedwait(sem_t *__sem, const struct timespec *abs_timeout)
{
	int ret = 0;
	uint64_t us = abs_timeout->tv_nsec/1000 + (abs_timeout->tv_sec)*1000000L;
	pthread_t pthread = (pthread_t)current_uthread;
	struct sem_queue_element e = {{0}, __sem, pthread, us, {0}, false};

	spin_pdr_lock(&__sem->lock);
	if(__sem->count > 0) {
		__sem->count--;
		spin_pdr_unlock(&__sem->lock);
	}
	else {
		/* We unlock in the body of __sem_block */
		uthread_yield(TRUE, __sem_timedblock, &e);

		/* Spin briefly to make sure that all references to e are
		 * gone between the post() and the timeout() code. We use
		 * e.awaiter.data to do this. */
		while (e.awaiter.data != NULL)
			cpu_relax();

		if (e.timedout) {
			errno = ETIMEDOUT;
			ret = -1;
		}
	}
	return ret;
}

int sem_trywait (sem_t *__sem)
{
	int ret = -1;
	spin_pdr_lock(&__sem->lock);
	if(__sem->count > 0) {
		__sem->count--;
		ret = 0;
	}
	spin_pdr_unlock(&__sem->lock);
	return ret;
}

int sem_post (sem_t *__sem)
{
	spin_pdr_lock(&__sem->lock);
	struct sem_queue_element *e = TAILQ_FIRST(&__sem->queue);
	if (e)
		TAILQ_REMOVE(&__sem->queue, e, next);
	else
		__sem->count++;	
	spin_pdr_unlock(&__sem->lock);

	if (e) {
		if(e->us_timeout != (uint64_t)-1) {
			/* Try and unset the alarm.  If this fails, then we
			 * have already started running the alarm callback.  If
			 * it succeeds, then we can set awaiter->data to NULL
			 * so that the bottom half of wake can proceed. Either
			 * we set awaiter->data to NULL or __sem_timeout
			 * does. The fact that we made it here though, means
			 * that WE are the one who removed e from the queue, so
			 * we are basically just deciding who should set
			 * awaiter->data to NULL to indicate that there are no
			 * more references to it. */
			if(unset_alarm(&e->awaiter))
				e->awaiter.data = NULL;
		}
		uthread_runnable((struct uthread*)e->pthread);
	}
	return 0;
}

int sem_getvalue (sem_t *__restrict __sem, int *__restrict __sval)
{
	spin_pdr_lock(&__sem->lock);
	*__sval = __sem->count;
	spin_pdr_unlock(&__sem->lock);
	return 0;
}

