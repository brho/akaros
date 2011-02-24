#ifndef _VCORE_H
#define _VCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/vcore.h>
#include <string.h>

/*****************************************************************************/
/* TODO: This is a complete hack, but necessary for vcore stuff tow ork for now
 * The issue is that exit sometimes calls sys_yield(), and we can't recover from
 * that properly under our vcore model (we shouldn't though).  We really need to
 * rethink what sys_yield 'should' do when in multicore mode, or else come up 
 * with a different syscall entirely. */
#include <stdlib.h>
#include <unistd.h>
#undef exit
#define exit(status) ros_syscall(SYS_proc_destroy, getpid(), status, 0, 0, 0, 0)
/*****************************************************************************/

#define LOG2_MAX_VCORES 6
#define MAX_VCORES (1 << LOG2_MAX_VCORES)

#define TRANSITION_STACK_PAGES 2
#define TRANSITION_STACK_SIZE (TRANSITION_STACK_PAGES*PGSIZE)

/* 2L-Scheduler operations.  Can be 0. */
struct schedule_ops {
	void (*preempt_pending)(void);
};
extern struct schedule_ops *sched_ops;

/* Defined by glibc; Must be implemented by a user level threading library */
extern void vcore_entry();

/* Utility Functions */
void *allocate_tls(void);

/* Vcore API functions */
int vcore_init(void);
int vcore_id(void);
int vcore_request(size_t k);
void vcore_yield(void);
size_t max_vcores(void);
size_t num_vcores(void);
bool check_preempt_pending(uint32_t vcoreid);

static inline void enable_notifs(uint32_t vcoreid)
{
	__procdata.vcore_preempt_data[vcoreid].notif_enabled = TRUE;
}

static inline void disable_notifs(uint32_t vcoreid)
{
	__procdata.vcore_preempt_data[vcoreid].notif_enabled = FALSE;
}

#ifdef __cplusplus
}
#endif

#endif
