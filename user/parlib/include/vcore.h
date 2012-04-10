#ifndef _VCORE_H
#define _VCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/vcore.h>
#include <arch/atomic.h>
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
static inline bool preempt_is_pending(uint32_t vcoreid);
static inline bool __preempt_is_pending(uint32_t vcoreid);
int vcore_init(void);
void vcore_event_init(void);
void vcore_change_to_m(void);
int vcore_request(long nr_new_vcores);
void vcore_yield(bool preempt_pending);
bool clear_notif_pending(uint32_t vcoreid);
void enable_notifs(uint32_t vcoreid);
void disable_notifs(uint32_t vcoreid);
void vcore_idle(void);
void ensure_vcore_runs(uint32_t vcoreid);
void cpu_relax_vc(uint32_t vcoreid);

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

/* Uthread's can call this in case they care if a preemption is coming.  If a
 * preempt is incoming, this will return TRUE, if you are in uthread context.  A
 * reasonable response for a uthread is to yield, and vcore_entry will deal with
 * the preempt pending.
 *
 * If you call this from vcore context, it will do nothing.  In general, it's
 * not safe to just yield (or do whatever you plan on doing) from arbitrary
 * places in vcore context.  So we just lie about PP. */
static inline bool preempt_is_pending(uint32_t vcoreid)
{
	if (in_vcore_context())
		return FALSE;
	return __preempt_is_pending(vcoreid);
}

static inline bool __preempt_is_pending(uint32_t vcoreid)
{
	return __procinfo.vcoremap[vcoreid].preempt_pending;
}


#ifndef __PIC__

#define begin_safe_access_tls_vars()

#define end_safe_access_tls_vars()

#else

#include <features.h>
#if __GNUC_PREREQ(4,4)

#define begin_safe_access_tls_vars()                                    \
  void __attribute__((noinline, optimize("O0")))                        \
  safe_access_tls_var_internal() {                                      \
    asm("");                                                            \

#define end_safe_access_tls_vars()                                      \
  } safe_access_tls_var_internal();                                     \

#else

#define begin_safe_access_tls_vars()                                                   \
  printf("ERROR: For PIC you must be using gcc 4.4 or above for tls support!\n");      \
  printf("ERROR: As a quick fix, try recompiling your application with -static...\n"); \
  exit(2);

#define end_safe_access_tls_vars()                                         \
  printf("Will never be called because we abort above!");                  \
  exit(2);

#endif //__GNUC_PREREQ
#endif // __PIC__

#define begin_access_tls_vars(tls_desc)                               \
{                                                                     \
	uthread_t *caller;                                            \
        int invcore = in_vcore_context();                             \
        if(!invcore) {                                                \
          caller = current_uthread;                                   \
	  assert(caller);                                             \
	  caller->flags |= UTHREAD_DONT_MIGRATE;                      \
        }                                                             \
                                                                      \
	cmb();                                                        \
	int vcoreid = vcore_id();                                     \
        void *temp_tls_desc = get_tls_desc(vcoreid);                  \
                                                                      \
        if(!invcore)                                                  \
	  disable_notifs(vcoreid);                                    \
                                                                      \
        set_tls_desc(tls_desc, vcoreid);                              \
        begin_safe_access_tls_vars();

#define end_access_tls_vars()                                         \
        end_safe_access_tls_vars();                                   \
        set_tls_desc(temp_tls_desc, vcoreid);                         \
                                                                      \
        if(!invcore) {                                                \
	  caller->flags &= ~UTHREAD_DONT_MIGRATE;                     \
          cmb();                                                      \
	  if(in_multi_mode())                                         \
            enable_notifs(vcoreid);                                   \
        }                                                             \
	cmb();                                                        \
}

#define safe_set_tls_var(name, val)                                   \
({                                                                    \
        begin_safe_access_tls_vars();                                 \
        name = val;                                                   \
        end_safe_access_tls_vars();                                   \
})

#define safe_get_tls_var(name)                                        \
({                                                                    \
        typeof(name) __val;                                           \
        begin_safe_access_tls_vars();                                 \
        __val = name;                                                 \
        end_safe_access_tls_vars();                                   \
        __val;                                                        \
})

#define vcore_set_tls_var(name, val)                                     \
{                                                                        \
      extern void** vcore_thread_control_blocks;                         \
      typeof(val) __val = val;                                           \
      begin_access_tls_vars(vcore_thread_control_blocks[vcoreid]);       \
      name = __val;                                                      \
      end_access_tls_vars();                                             \
}

#define vcore_get_tls_var(name)                                 \
({                                                              \
      typeof(name) val;                                         \
      begin_access_tls_vars(vcore_tls_descs[vcoreid]);          \
      val = name;                                               \
      end_access_tls_vars();                                    \
      val;                                                      \
})

#ifdef __cplusplus
}
#endif

#endif
