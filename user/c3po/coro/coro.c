/*
    Coroutine implementation for x86/linux, gcc-2.7.2

    Copyright 1999 by E. Toernig <froese@gmx.de>
*/

#include <unistd.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <execinfo.h>
#include "coro.h"

#if !defined(i386)
#error For x86-CPUs only
#endif

#if !defined(__GNUC__)
#warning May break without gcc.  Be careful.
#endif

/* asm-name for identifier x */
#if 1
#define _(x) #x
#else
#define _(x) "_"#x
#endif

struct coroutine co_main[1] = { { NULL, NULL, NULL, NULL, NULL, 0 } };
struct coroutine *co_current = co_main;


#define fatal(msg) \
    for (;;)						\
    {							\
	  int fee1dead = write(2, "coro: " msg "\r\n", sizeof(msg)+7);	\
	  *(unsigned int *)0 = fee1dead; \
    }



/*
    Create new coroutine.
    'func' is the entry point
    'stack' is the start of the coroutines stack.  if 0, one is allocated.
    'size' is the size of the stack
*/


static void wrap(void *data) __attribute__((noreturn,regparm(1)));

static void __attribute__((noreturn,regparm(1))) 
wrap(void *data) /* arg in %eax! */
{
    co_current->resumeto = co_current->caller;

    for (;;)
	data = co_resume(co_current->func(data));
}

struct coroutine *
co_create(void *func, void *stack, int size)
{
    struct coroutine *co;
    int to_free = 0;

    if (size < 128)
	return 0;

    if (stack == 0)
    {
	size += 4096-1;
	size &= ~(4096-1);
	stack = mmap(0, size, PROT_READ|PROT_WRITE,
			      MAP_PRIVATE|MAP_ANON, -1, 0);
	if (stack == (void*)-1)
	    return 0;

	to_free = size;
    }
    co = stack + size;
    co = (struct coroutine*)((unsigned long)co & ~3);
    co -= 1;

    co->sp = co;
    co->caller = 0;
    co->resumeto = 0;
    co->user = 0;
    co->func = func;
    co->to_free = to_free;

    co->sp = ((void**)co->sp)-1; *(void**)co->sp = wrap;  // return addr (here: start addr)
    co->sp = ((void**)co->sp)-1; *(void**)co->sp = 0;     // ebp
    co->sp = ((void**)co->sp)-1; *(void**)co->sp = 0;     // ebx
    co->sp = ((void**)co->sp)-1; *(void**)co->sp = 0;     // esi
    co->sp = ((void**)co->sp)-1; *(void**)co->sp = 0;     // edi
    return co;
}



/*
    delete a coroutine.
*/

void
co_delete(struct coroutine *co)
{
    if (co == co_current)
	fatal("coroutine deletes itself");

    if (co->to_free)
	munmap((void *)co + sizeof(*co) - co->to_free, co->to_free);
}



/*
    delete self and switch to 'new_co' passing 'data'
*/

static void *helper_args[2];

static void
del_helper(void **args)
{
    for (;;)
    {
	if (args != helper_args)
	    fatal("resume to deleted coroutine");
	co_delete(co_current->caller);
	args = co_call(args[0], args[1]);
    }
}

void
co_exit_to(struct coroutine *new_co, void *data)
{
    static struct coroutine *helper = 0;
    static char stk[512]; // enough for a kernel call and a signal handler

    // FIXME: multi-cpu race on use of helper!!  Solve by creating an array of helpers for each CPU
    helper_args[0] = new_co;
    helper_args[1] = data;

    if (helper == 0)
	helper = co_create(del_helper, stk, sizeof(stk));

    // we must leave this coroutine.  so call the helper.
    co_call(helper, helper_args);
    fatal("stale coroutine called");
}

void
co_exit(void *data)
{
    co_exit_to(co_current->resumeto, data);
}



/*
    Call other coroutine.
    'new_co' is the coroutine to switch to
    'data' is passed to the new coroutine
*/

//void *co_call(struct coroutine *new_co, void *data) { magic }
asm(	".text"				);
asm(	".align 16"			);
asm(	".globl "_(co_call)		);
asm(	".type "_(co_call)",@function"	);
asm(	_(co_call)":"			);

asm(	"pushl %ebp"			);	// save reg-vars/framepointer
asm(	"pushl %ebx"			);
asm(	"pushl %esi"			);
asm(	"pushl %edi"			);

asm(	"movl "_(co_current)",%eax"	);	// get old co
asm(	"movl %esp,(%eax)"		);	// save sp of old co

asm(	"movl 20(%esp),%ebx"		);	// get new co (arg1)
asm(	"movl %ebx,"_(co_current)	);	// set as current
asm(	"movl %eax,4(%ebx)"		);	// save caller
asm(	"movl 24(%esp),%eax"		);	// get data (arg2)
asm(	"movl (%ebx),%esp"		);	// restore sp of new co

asm(	"popl %edi"			);	// restore reg-vars/frameptr
asm(	"popl %esi"			);
asm(	"popl %ebx"			);
asm(	"popl %ebp"			);
asm(	"ret"				);	// return to new coro

asm(	"1:"				);
asm(	".size "_(co_call)",1b-"_(co_call));


void *_co_esp, *_co_ebp, **_co_array;   // temp save place for co_backtrace
static int _co_size, _co_rv;

// we do this by temporarily switch to the coroutine and call backtrace()
int co_backtrace(struct coroutine *cc, void **array, int size)
{
  (void) _co_esp; // avoid unused var warning
  (void) _co_ebp;

  if (cc == co_current)
    return backtrace(array, size);
      
  // save arguments to global variable so we do not need to use the stack
  // when calling backtrace()
  _co_array = array;
  _co_size = size;

  asm(  "movl %ebp, "_(_co_ebp)      );   // save %ebp
  asm(  "movl %esp, "_(_co_esp)      );   // save %esp
  asm(  "movl (%%eax), %%esp" 
		  : /* no output */ 
		  : "a" (cc)         );   // set %esp to the co-routine
  asm(  "movl %esp, %ebp"            );
  asm(  "add  $12, %ebp"              );   // this is where co_call() saved the previous $ebp!
  
  _co_rv = backtrace(_co_array, _co_size);

  asm(  "movl "_(_co_esp)", %esp"  );   // restore %esp
  asm(  "movl "_(_co_ebp)", %ebp"  );   // restore %ebp

  return _co_rv;
}


void *
co_resume(void *data)
{
    data = co_call(co_current->resumeto, data);
    co_current->resumeto = co_current->caller;
    return data;
}


