#include <parlib/uthread.h>
#include <parlib/assert.h>
#include <semaphore.h>
#include <parlib/stdio.h>
#include <errno.h>

int sem_init(sem_t *__sem, int __pshared, unsigned int __value)
{
	if (__pshared == TRUE) {
		printf("__pshared functionality of sem_init is not yet implemented!");
		return -1;
	}
	uth_semaphore_init(&__sem->real_sem, __value);
	return 0;
}

int sem_destroy(sem_t *__sem)
{
	uth_semaphore_destroy(&__sem->real_sem);
	return 0;
}

sem_t *sem_open(__const char *__name, int __oflag, ...)
{
	printf("sem_open is not yet implemented!");
	return NULL;
}

int sem_close(sem_t *__sem)
{
	printf("sem_close is not yet implemented!");
	return -1;
}

int sem_unlink(__const char *__name)
{
	printf("sem_unlink is not yet implemented!");
	return -1;
}

int sem_wait(sem_t *__sem)
{
	uth_semaphore_down(&__sem->real_sem);
	return 0;
}

int sem_timedwait(sem_t *__sem, const struct timespec *abs_timeout)
{
	if (!uth_semaphore_timed_down(&__sem->real_sem, abs_timeout)) {
		errno = ETIMEDOUT;
		return -1;
	}
	return 0;
}

int sem_trywait(sem_t *__sem)
{
	if (!uth_semaphore_trydown(&__sem->real_sem))
		return -1;
	return 0;
}

int sem_post(sem_t *__sem)
{
	uth_semaphore_up(&__sem->real_sem);
	return 0;
}

int sem_getvalue(sem_t *__restrict __sem, int *__restrict __sval)
{
	spin_pdr_lock(&__sem->real_sem.lock);
	*__sval = __sem->real_sem.count;
	spin_pdr_unlock(&__sem->real_sem.lock);
	return 0;
}
