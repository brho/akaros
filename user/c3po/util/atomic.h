/**
 * Assembly language for x86 atomic operations.  These were coopted
 * from the glibc include files for linux, as noted below.  We have
 * included them here (unmodified) because the include files only
 * define them when in kernel mode.
 **/

#include <unistd.h>
#include <ros/syscall.h>

// from /usr/include/asm/linux/autoconf.h
#define CONFIG_SMP 1
#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock ; "
#else
#define LOCK_PREFIX ""
#endif


// Everything below is from /usr/include/asm/system.h


struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))


/*
 * Note: no "lock" prefix even on SMP: xchg always implies lock anyway
 * Note 2: xchg has side effect, so that attribute volatile is necessary,
 *	  but generally the primitive is invalid, *ptr is output argument. --ANK
 */
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 1:
			__asm__ __volatile__("xchgb %b0,%1"
				:"=q" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 2:
			__asm__ __volatile__("xchgw %w0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 4:
			__asm__ __volatile__("xchgl %0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
	}
	return x;
}

#define xchg(ptr,v) ((__typeof__(*(ptr)))__xchg((unsigned long)(v),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

static inline void spinlock_lock(int *ptr) 
{ 
  while( 1 ) {
    register int i=1000;
    while( i && (tas(ptr) == 1) ) 
      i--; 
    if( i ) 
      return;
    syscall(SYS_yield);
  }
}
#define spinlock_unlock(ptr) { *(ptr)=0; }



/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long prev;
	switch (size) {
	case 1:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgb %b1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 2:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgw %w1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 4:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgl %1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	}
	return old;
}
/*
int atomic_h_mutex=0;
static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
  unsigned long ret;
  (void) size;
  spinlock_lock( &atomic_h_mutex );
  // assume size == sizeof(long)
  if( *((unsigned long*)ptr) == old ) {
    *((unsigned long*)ptr) = new;
    ret = old;
  } else {
    ret = *((unsigned long*)ptr);
  }
  spinlock_unlock( &atomic_h_mutex );
  return ret;
}
*/

#define cmpxchg(ptr,o,n)\
	((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),\
					(unsigned long)(n),sizeof(*(ptr))))


// FIXME: does this work?
#define SERIALIZATION_BARRIER() { __asm__("cpuid"); }
