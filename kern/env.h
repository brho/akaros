/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_ENV_H
#define ROS_KERN_ENV_H

#include <inc/x86.h>
#include <inc/env.h>

#ifndef ROS_MULTIENV
// Change this value to 1 once you're allowing multiple environments
// (for UCLA: Lab 3, Part 3; for MIT: Lab 4).
#define ROS_MULTIENV 0
#endif

extern env_t *envs;		// All environments
extern env_t* NORACE curenvs[MAX_NUM_CPUS];

LIST_HEAD(env_list_t, env_t);		// Declares 'struct Env_list'

void	env_init(void);
int		env_alloc(env_t **e, envid_t parent_id);
void	env_free(env_t *e);
void	env_create(uint8_t *binary, size_t size);
void	(IN_HANDLER env_destroy)(env_t *e);	// Does not return if e == curenv

int	envid2env(envid_t envid, env_t **env_store, bool checkperm);
// The following two functions do not return
void	(IN_HANDLER env_run)(env_t *e) __attribute__((noreturn));
void	env_pop_tf(trapframe_t *tf) __attribute__((noreturn));

// For the grading script
#define ENV_CREATE2(start, size)	{		\
	extern uint8_t start[], size[];			\
	env_create(start, (int)size);			\
}

#define ENV_CREATE(x)			{		\
	extern uint8_t _binary_obj_##x##_start[],	\
		_binary_obj_##x##_size[];		\
	env_create(_binary_obj_##x##_start,		\
		(int)_binary_obj_##x##_size);		\
}

#endif // !ROS_KERN_ENV_H
