#ifndef CORO_H
#define CORO_H

struct coroutine
{
    void *sp;			/* saved stack pointer while coro is inactive */
    struct coroutine *caller;	/* PUBLIC who has called this coroutine */
    struct coroutine *resumeto;	/* PUBLIC who to resume to */
    void *user;			/* PUBLIC user data.  for whatever you want. */

    void *(*func)(void *);	/* coroutines main function */
    int to_free;		/* how much memory to free on co_delete */
};

extern struct coroutine *co_current;	/* currently active coroutine */
extern struct coroutine co_main[];	/* automagically generated main coro. */

struct coroutine *co_create(void *func, void *stack, int stacksize);
void *co_call(struct coroutine *co, void *data);
void *co_resume(void *data);
void co_delete(struct coroutine *co);
void co_exit_to(struct coroutine *co, void *data) __attribute__((noreturn));
void co_exit(void *data) __attribute__((noreturn));

/* Store up to SIZE return addresses of the stack state of coroutine CC and return 
 * the number of values stored - zf */
int co_backtrace(struct coroutine *cc, void **array, int size);

#endif
