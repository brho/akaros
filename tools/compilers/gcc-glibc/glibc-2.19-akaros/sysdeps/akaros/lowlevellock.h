#ifndef _LOWLEVELLOCK_H
#define _LOWLEVELLOCK_H

#include <atomic.h>
#include <sys/param.h>
#include <parlib/spinlock.h>

/* Akaros doesn't support private vs shared locks.  That's probably a problem */
#define LLL_PRIVATE 0
#define LLL_SHARED 1

/* Glibc's locking is a nightmare, and in lieu of rewriting that mess (see
 * libc-lock.h) to support something other than an integer for the LLL lock
 * (and thus the libc lock, and the libc recursive lock, etc), we'll just use
 * an int for storage and cast that to a struct spin_pdr_lock, which happens to
 * also be 32 bits.  FFS. */
#define LLL_LOCK_INITIALIZER SPINPDR_UNLOCKED

#define lll_lock(l, p) spin_pdr_lock((struct spin_pdr_lock*)&(l))
#define lll_unlock(l, p) spin_pdr_unlock((struct spin_pdr_lock*)&(l))
/* lll_trylock returns 0 on success.  spin_pdr_trylock returns TRUE on
 * success. */
#define lll_trylock(l) !spin_pdr_trylock((struct spin_pdr_lock*)&l)

#define lll_futex_wait(m,v,p) do { assert("NO FUTEX_WAIT FOR YOU!" == 0); } while(0)
#define lll_futex_wake(m,n,p) do { assert("NO FUTEX_WAKE FOR YOU!" == 0); } while(0)

#endif
