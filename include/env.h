/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_ENV_H
#define ROS_KERN_ENV_H

#include <arch/x86.h>
#include <ros/env.h>
#include <ros/error.h>

extern env_t *COUNT(NENV) envs;		// All environments
extern uint32_t num_envs;		// Number of envs
extern env_t* NORACE curenvs[MAX_NUM_CPUS];

LIST_HEAD(env_list, Env);		// Declares 'struct env_list'
typedef struct env_list env_list_t;

void	env_init(void);
int		env_alloc(env_t **e, envid_t parent_id);
void	env_free(env_t *SAFE e);
error_t	env_incref(env_t* e);
void	env_decref(env_t *SAFE e);
env_t*	env_create(uint8_t *COUNT(size) binary, size_t size);
void	(IN_HANDLER env_destroy)(env_t *SAFE e);	// Does not return if e == curenv
// Temporary scheduler function
void	schedule(void);

/*
 * Allows the kernel to figure out what process is running on its core.
 * Can be used just like a pointer to a struct process.
 */
#define current (curenvs[lapic_get_id()])

int	envid2env(envid_t envid, env_t **env_store, bool checkperm);
// The following three functions do not return
void	(IN_HANDLER env_run)(env_t *e) __attribute__((noreturn));
void	env_pop_tf(trapframe_t *tf) __attribute__((noreturn));
void	env_pop_tf_sysexit(trapframe_t *tf) __attribute__((noreturn));


/* Helper handler for smp_call to dispatch jobs to other cores */
void run_env_handler(trapframe_t *tf, void* data);

#define ENV_CREATE(x)			({                                             \
	extern uint8_t _binary_obj_user_apps_##x##_start[],                        \
		_binary_obj_user_apps_##x##_size[];                                    \
	env_t *e = env_create(_binary_obj_user_apps_##x##_start,                   \
		(int)_binary_obj_user_apps_##x##_size);                                \
	e->env_status = ENV_RUNNABLE;                                              \
	e;                                                                         \
})

#endif // !ROS_KERN_ENV_H
