/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_ENV_H
#define ROS_KERN_ENV_H

#include <ros/env.h>
#include <ros/error.h>
#include <arch/arch.h>

extern env_t *COUNT(NENV) envs;		// All environments
extern atomic_t num_envs;		// Number of envs
extern env_t* NORACE curenvs[MAX_NUM_CPUS];

LIST_HEAD(env_list, Env);		// Declares 'struct env_list'
typedef struct env_list env_list_t;

void	env_init(void);
void	env_init_trapframe(env_t* e);
void	env_set_program_counter(env_t* e, uintptr_t pc);
void	env_push_ancillary_state(env_t* e);
void	env_pop_ancillary_state(env_t* e);
int	env_alloc(env_t **e, envid_t parent_id);
void	env_free(env_t *SAFE e);
void	env_user_mem_free(env_t* e);
error_t	env_incref(env_t* e);
void	env_decref(env_t *SAFE e);
env_t*	env_create(uint8_t *COUNT(size) binary, size_t size);
void	(IN_HANDLER env_destroy)(env_t *SAFE e);	// Does not return if e == curenv
// Temporary scheduler function
void	schedule(void);

int	envid2env(envid_t envid, env_t **env_store, bool checkperm);
// The following three functions do not return
void	(IN_HANDLER env_run)(env_t *e) __attribute__((noreturn));
void	env_pop_tf(trapframe_t *tf) __attribute__((noreturn));


/* Helper handler for smp_call to dispatch jobs to other cores */
void run_env_handler(trapframe_t *tf, void* data);

#define ENV_CREATE(x)			({                                             \
	extern uint8_t _binary_obj_user_apps_##x##_start[],                        \
		_binary_obj_user_apps_##x##_size[];                                    \
	env_create(_binary_obj_user_apps_##x##_start,                              \
		(int)_binary_obj_user_apps_##x##_size);                                \
})

#endif // !ROS_KERN_ENV_H
