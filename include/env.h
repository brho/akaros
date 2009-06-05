/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_ENV_H
#define ROS_KERN_ENV_H

#include <arch/x86.h>
#include <ros/env.h>
#include <ros/error.h>

extern env_t *envs;		// All environments
extern env_t* NORACE curenvs[MAX_NUM_CPUS];

LIST_HEAD(env_list_t, env_t);		// Declares 'struct Env_list'

void	env_init(void);
int		env_alloc(env_t **e, envid_t parent_id);
void	env_free(env_t *e);
error_t	env_incref(env_t* e);
void	env_decref(env_t* e);
env_t*	env_create(uint8_t *binary, size_t size);
void	(IN_HANDLER env_destroy)(env_t *e);	// Does not return if e == curenv

int	envid2env(envid_t envid, env_t **env_store, bool checkperm);
// The following two functions do not return
void	(IN_HANDLER env_run)(env_t *e) __attribute__((noreturn));
void	env_pop_tf(trapframe_t *tf) __attribute__((noreturn));

#define ENV_CREATE(x)			({                                             \
	extern uint8_t _binary_obj_user_apps_##x##_start[],                        \
		_binary_obj_user_apps_##x##_size[];                                    \
	env_create(_binary_obj_user_apps_##x##_start,                              \
		(int)_binary_obj_user_apps_##x##_size);                                \
})

#endif // !ROS_KERN_ENV_H
