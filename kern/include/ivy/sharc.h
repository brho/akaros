#include <assert.h>
#include <string.h>

#ifdef IVY_FAST_CHECKS
  #define SINLINE __attribute__((always_inline))
#else
  #define SINLINE inline
#endif

#define SUNUSED __attribute__((unused))

#ifndef sasmlinkage
#define sasmlinkage __attribute__((regparm(0)))
#endif

#ifndef snoreturn
#define snoreturn __attribute__((noreturn))
#endif

typedef struct __ivy_sharC_thread {
#define SHARC_MAX_LOCKS 16
    const void *held_locks[SHARC_MAX_LOCKS];
    unsigned int max_lock;
} sharC_env_t;

#include <smp.h>
#include <process.h>

extern int booting;
extern int __ivy_checking_on;

#pragma cilnoremove("sharC_env_init")
static SINLINE void sharC_env_init(sharC_env_t *sharC_env) TRUSTED;
static SINLINE void sharC_env_init(sharC_env_t *sharC_env)
WRITES(sharC_env->max_lock,sharC_env->held_locks)
{
	sharC_env->max_lock = 0;
	memset(sharC_env->held_locks,0,SHARC_MAX_LOCKS);
	return;
}

static __attribute__((always_inline)) int
is_single_threaded() TRUSTED
{
	return booting || (num_idlecores == num_cpus - 1);
}

extern void sasmlinkage
__sharc_single_thread_error_mayreturn(const char *msg);

extern void sasmlinkage snoreturn
__sharc_single_thread_error_noreturn(const char *msg);

#ifdef IVY_FAST_CHECKS
#define __sharc_single_thread_error __sharc_single_thread_error_noreturn
#else
#define __sharc_single_thread_error __sharc_single_thread_error_mayreturn
#endif

#pragma cilnoremove("__sharc_single_threaded")
static SINLINE void __sharc_single_threaded(const void *msg) TRUSTED;
static SINLINE void __sharc_single_threaded(const void *msg)
{
	if (is_single_threaded()) return;
	__sharc_single_thread_error(msg);
    return;
}

#define sharc_current      (&per_cpu_info[core_id()])
#define GET_SHARC_THREAD() sharc_current->sharC_env

#define THREAD_LOCKS(thread,i) (thread.held_locks[(i)])
#define THREAD_MAX_LOCK(thread) (thread.max_lock)

#define THIS_LOCKS(i)        (THREAD_LOCKS(GET_SHARC_THREAD(),(i)))
#define THIS_MAX_LOCK        (THREAD_MAX_LOCK(GET_SHARC_THREAD()))

/*
 * Locks
 */

extern void sasmlinkage snoreturn
__sharc_lock_error_noreturn(const void *lck, const void *what,
                            unsigned int sz, const char *msg);

extern void sasmlinkage
__sharc_lock_error_mayreturn(const void *lck, const void *what,
                             unsigned int sz, const char *msg);

extern void sasmlinkage snoreturn
__sharc_lock_coerce_error_noreturn(const void *dstlck, const void *srclck,
                                   const char *msg);

extern void sasmlinkage
__sharc_lock_coerce_error_mayreturn(const void *dstlck, const void *srclck,
                                    const char *msg);

#ifdef IVY_FAST_CHECKS
#define __sharc_lock_error         __sharc_lock_error_noreturn
#define __sharc_lock_coerce_error  __sharc_lock_coerce_error_noreturn
#else
#define __sharc_lock_error         __sharc_lock_error_mayreturn
#define __sharc_lock_coerce_error  __sharc_lock_coerce_error_mayreturn
#endif

/* assumes no double-locking */
#pragma cilnoremove("__sharc_add_lock")
static SINLINE void __sharc_add_lock(const void *lck) TRUSTED;
static SINLINE void __sharc_add_lock(const void *lck)
{
    unsigned int i;

	if (!__ivy_checking_on || is_single_threaded()) return;

    for (i = 0; i <= THIS_MAX_LOCK; i++)
        if (!THIS_LOCKS(i))
            break;

    if (i > THIS_MAX_LOCK && THIS_MAX_LOCK < SHARC_MAX_LOCKS)
            THIS_MAX_LOCK++;

    THIS_LOCKS(i) = lck;
    return;
}

/* this will be very inefficient if the lock isn't actually held */
#pragma cilnoremove("__sharc_rm_lock")
static SINLINE void __sharc_rm_lock(const void *lck) TRUSTED;
static SINLINE void __sharc_rm_lock(const void *lck)
{
    unsigned int i;

	if (!__ivy_checking_on || is_single_threaded()) return;

    for (i = 0; i <= THIS_MAX_LOCK; i++)
        if (THIS_LOCKS(i) == lck)
            break;

    if (i == THIS_MAX_LOCK && THIS_MAX_LOCK > 0)
            THIS_MAX_LOCK--;

    THIS_LOCKS(i) = (void *)0;
    return;
}

#pragma cilnoremove("__sharc_chk_lock")
static SINLINE void
__sharc_chk_lock(const void *lck, const void *what, unsigned int sz,
                 const char *msg) TRUSTED;
static SINLINE void
__sharc_chk_lock(const void *lck, const void *what, unsigned int sz,
                 const char *msg)
{
    unsigned int i;

	// TODO: how do I find how many threads are running?
    //if (__sharc_num_threads == 1) return;

	if (!__ivy_checking_on || is_single_threaded()) return;

    for (i = 0; i <= THIS_MAX_LOCK; i++)
        if (THIS_LOCKS(i) == lck)
            break;

    if (i > THIS_MAX_LOCK) {
            __sharc_lock_error(lck,what,sz,msg);
    }
}

#pragma cilnoremove("__sharc_coerce_lock")
static SINLINE void
__sharc_coerce_lock(const void *dstlck, const void *srclck,
                    const char *msg) TRUSTED;
static SINLINE void
__sharc_coerce_lock(const void *dstlck, const void *srclck,
                    const char *msg)
{
	if (!__ivy_checking_on || is_single_threaded()) return;

    if (dstlck != srclck)
        __sharc_lock_coerce_error(dstlck,srclck,msg);
}

/*
 * The sharing cast.
 *
 */

extern void __sharc_group_cast_error(int, void *, void *, char *);

#pragma cilnoremove("__sharc_check_group_cast")
static inline void
__sharc_check_group_cast(int hassame, void *srcg, void *src, char *msg) TRUSTED;
static inline void
__sharc_check_group_cast(int hassame, void *srcg, void *src, char *msg)
{
	int old;
	if (!__ivy_checking_on) return;
	old = __ivy_checking_on;
	__ivy_checking_on = 0;
	panic("sharc group cast unimplemented");
	__ivy_checking_on = old;
}


extern void __sharc_cast_error(void *addr, unsigned long sz, char *msg);

#pragma cilnoremove("__sharc_sharing_cast")
static SINLINE void *
__sharc_sharing_cast(void *addr,void **slot, unsigned int localslot SUNUSED,
                     unsigned long lo, unsigned long hi,
                     char *msg) TRUSTED;
static SINLINE void *
__sharc_sharing_cast(void *addr,void **slot, unsigned int localslot SUNUSED,
                     unsigned long lo, unsigned long hi,
                     char *msg)
{
	int old;
	if (!__ivy_checking_on) return NULL;
	old = __ivy_checking_on;
	__ivy_checking_on = 0;
	panic("sharc sharing cast unimplemented");
	__ivy_checking_on = old;
	return NULL;
}
