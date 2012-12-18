#include <uthread.h>
#include <semaphore.h>
#include <mcs.h>
#include <stdio.h>

int sem_init (sem_t *__sem, int __pshared, unsigned int __value)
{
	if(__pshared == TRUE) {
		printf("__pshared functionality of sem_init is not yet implemented!");
		return -1;
	}
	__sem->count = __value;
	TAILQ_INIT(&__sem->queue);
	mcs_pdr_init(&__sem->lock);
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

static void __sem_block(struct uthread *uthread, void *arg) {
	sem_t *__sem = (sem_t*)arg;
	pthread_t pthread = (pthread_t)uthread;
	pthread->state = PTH_BLK_MUTEX;
	TAILQ_INSERT_TAIL(&__sem->queue, pthread, next);
	mcs_pdr_unlock(&__sem->lock);
}

int sem_wait (sem_t *__sem)
{
	mcs_pdr_lock(&__sem->lock);
	if(__sem->count > 0) {
		__sem->count--;
		mcs_pdr_unlock(&__sem->lock);
	}
	else {
		// We unlock in the body of __sem_block
		uthread_yield(TRUE, __sem_block, __sem);
	}
	return 0;
}

int sem_trywait (sem_t *__sem)
{
	int ret = -1;
	mcs_pdr_lock(&__sem->lock);
	if(__sem->count > 0) {
		__sem->count--;
		ret = 0;
	}
	mcs_pdr_unlock(&__sem->lock);
	return ret;
}

int sem_post (sem_t *__sem)
{
	mcs_pdr_lock(&__sem->lock);
	pthread_t pthread = TAILQ_FIRST(&__sem->queue);
	if(pthread)
		TAILQ_REMOVE(&__sem->queue, pthread, next);
	else
		__sem->count++;	
	mcs_pdr_unlock(&__sem->lock);

	if(pthread) {
		uthread_runnable((struct uthread*)pthread);
	}
	return 0;
}

int sem_getvalue (sem_t *__restrict __sem, int *__restrict __sval)
{
	mcs_pdr_lock(&__sem->lock);
	*__sval = __sem->count;
	mcs_pdr_unlock(&__sem->lock);
	return 0;
}

