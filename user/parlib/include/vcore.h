#ifndef _VCORE_H
#define _VCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/vcore.h>
#include <sys/param.h>
#include <string.h>

/*****************************************************************************/
/* TODO: This is a complete hack, but necessary for vcore stuff to work for now
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

/* Defined by glibc; Must be implemented by a user level threading library */
extern void vcore_entry();
/* Declared in glibc's start.c */
extern __thread bool __vcore_context;

/* Utility Functions */
void *allocate_tls(void);
void free_tls(void *tcb);
void *reinit_tls(void *tcb);

/* Vcore API functions */
static inline size_t max_vcores(void);
static inline size_t num_vcores(void);
static inline int vcore_id(void);
static inline bool in_vcore_context(void);
static inline bool in_multi_mode(void);
static inline void __enable_notifs(uint32_t vcoreid);
static inline void __disable_notifs(uint32_t vcoreid);
static inline bool notif_is_enabled(uint32_t vcoreid);
static inline bool vcore_is_mapped(uint32_t vcoreid);
static inline bool vcore_is_preempted(uint32_t vcoreid);
static inline struct preempt_data *vcpd_of(uint32_t vcoreid);
int vcore_init(void);
int vcore_request(long nr_new_vcores);
void vcore_yield(bool preempt_pending);
bool clear_notif_pending(uint32_t vcoreid);
void enable_notifs(uint32_t vcoreid);
void disable_notifs(uint32_t vcoreid);
void vcore_idle(void);

/* Static inlines */
static inline size_t max_vcores(void)
{
	return MIN(__procinfo.max_vcores, MAX_VCORES);
}

static inline size_t num_vcores(void)
{
	return __procinfo.num_vcores;
}

static inline int vcore_id(void)
{
	return __vcoreid;
}

static inline bool in_vcore_context(void)
{
	return __vcore_context;
}

static inline bool in_multi_mode(void)
{
	return __procinfo.is_mcp;
}

/* Only call this if you know what you are doing. */
static inline void __enable_notifs(uint32_t vcoreid)
{
	vcpd_of(vcoreid)->notif_disabled = FALSE;
}

static inline void __disable_notifs(uint32_t vcoreid)
{
	vcpd_of(vcoreid)->notif_disabled = TRUE;
}

static inline bool notif_is_enabled(uint32_t vcoreid)
{
	return !vcpd_of(vcoreid)->notif_disabled;
}

static inline bool vcore_is_mapped(uint32_t vcoreid)
{
	return __procinfo.vcoremap[vcoreid].valid;
}

/* We could also check for VC_K_LOCK, but that's a bit much. */
static inline bool vcore_is_preempted(uint32_t vcoreid)
{
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	return atomic_read(&vcpd->flags) & VC_PREEMPTED;
}

static inline struct preempt_data *vcpd_of(uint32_t vcoreid)
{
	return &__procdata.vcore_preempt_data[vcoreid];
}

#ifdef __cplusplus
}
#endif

#endif
