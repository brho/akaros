#ifndef ROS_INC_MEASURE_H
#define ROS_INC_MEASURE_H

#include <inc/types.h>
#include <inc/stdio.h>
#include <inc/x86.h>

#define measure_function(func, iters, name)                                    \
({                                                                             \
	uint64_t ticks;                                                            \
	cpuid(0, 0, 0, 0, 0);                                                      \
	ticks = read_tsc();                                                        \
	/* Run this a bunch of times to make sure its accurate */                  \
	for(int i=0; i< (iters); i++) {                                            \
		func ;                                                                 \
	}                                                                          \
	cpuid(0, 0, 0, 0, 0);                                                      \
	ticks = read_tsc() - ticks;                                                \
	/* Compute the average and print it */                                     \
	uint64_t a = (1000000000LL/(iters) * ticks) / (env->env_tscfreq);          \
	if ((name))                                                                \
		cprintf("Measuring %s:\n"                                              \
		        "    Total ticks:          %20lld\n"                           \
		        "    Num Iterations:       %20d\n"                             \
		        "    Time Per Iteration:   %20lld\n",                          \
	        name, ticks, (iters), a);                                          \
	ticks;                                                                     \
})

#define measure_function_async(func, _desc_type, _rsp_type, wait_func,         \
	                           desc_name, i_iters, o_iters, name)              \
({                                                                             \
	uint64_t ticks;                                                            \
	_desc_type* desc_array[i_iters];                                           \
	_desc_type* desc_name;                                                     \
	/* Could use an array of rsps, but we're throwing away the responses*/     \
	_rsp_type rsp;                                                             \
	error_t err;                                                               \
	uint32_t safe_i_iters = i_iters;                                           \
	cpuid(0, 0, 0, 0, 0);                                                      \
	ticks = read_tsc();                                                        \
	/* Run this a bunch of times to make sure its accurate */                  \
	for(int i=0; i< (o_iters); i++) {                                          \
		for (int j = 0; j < (safe_i_iters); j++) {                             \
			err = func ;                                                       \
			if (err)                                                           \
				(safe_i_iters) = j;                                            \
			desc_array[j] = desc_name;                                         \
		}                                                                      \
		for (int j = 0; j < (safe_i_iters); j++) {                             \
			wait_func(desc_array[j], &rsp);                                    \
		}                                                                      \
	}                                                                          \
	cpuid(0, 0, 0, 0, 0);                                                      \
	ticks = read_tsc() - ticks;                                                \
	/* Compute the average and print it */                                     \
	uint64_t a = (1000000000LL/((o_iters)*(safe_i_iters)) * ticks) /           \
	             (env->env_tscfreq);                                           \
	if ((name))                                                                \
		cprintf("Measuring %s:\n"                                              \
		        "    Total ticks:          %20lld\n"                           \
		        "    Num Total Iterations: %20d\n"                             \
		        "    Num Inner Iterations: %20d\n"                             \
		        "    Time Per Iteration:   %20lld\n",                          \
	        name, ticks, ((o_iters)*(safe_i_iters)), safe_i_iters, a);         \
	ticks;                                                                     \
})

#define measure_async_call(func, desc_name, i_iters, o_iters, name)            \
	measure_function_async(func, async_desc_t, async_rsp_t, waiton_async_call, \
	                       desc_name, i_iters, o_iters, name)

#endif /* !ROS_INC_MEASURE_H */
