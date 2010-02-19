#ifndef _LOWLEVELLOCK_H
#define _LOWLEVELLOCK_H

#include <atomic.h>
#include <ros/arch/hart.h>

#define LLL_PRIVATE 0
#define LLL_SHARED 1

#define LLL_LOCK_INITIALIZER (0)

#define lll_lock(l,p) do { } while(lll_trylock(l))
#define lll_unlock(l,p) ({ (l) = 0; 0; })
#define lll_trylock(l) __hart_swap(&(l),1)

#define lll_futex_wait(m,v,p) do { assert("NO FUTEX_WAIT FOR YOU!" == 0); } while(0)
#define lll_futex_wake(m,n,p) do { assert("NO FUTEX_WAKE FOR YOU!" == 0); } while(0)

#endif
