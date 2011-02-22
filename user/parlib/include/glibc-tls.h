/* This is just a convenient place to drop some structs needed in
 * allocate_tls(), since we need to know where tcb and self are, without
 * cluttering up the main ROS code */

#ifndef _GLIBC_TLS_H
#define _GLIBC_TLS_H

/* Type for the dtv.  */
typedef union dtv
{
  size_t counter;
  struct
  {
    void *val;
    bool is_static;
  } pointer;
} dtv_t;


typedef struct
{
  void *tcb;		/* Pointer to the TCB.  Not necessarily the
			   thread descriptor used by libpthread.  */
  dtv_t *dtv;
  void *self;		/* Pointer to the thread descriptor.  */
  int multiple_threads;
  uintptr_t sysinfo;
  uintptr_t stack_guard;
  uintptr_t pointer_guard;
  int gscope_flag;
#ifndef __ASSUME_PRIVATE_FUTEX
  int private_futex;
#else
  int __unused1;
#endif
  /* Reservation of some values for the TM ABI.  */
  void *__private_tm[5];
} tcbhead_t;

#endif /* _GLIBC_TLS_H */
