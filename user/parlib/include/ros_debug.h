#ifndef PARLIB_ROS_DEBUG_H
#define PARLIB_ROS_DEBUG_H

#include <parlib/common.h>
#include <parlib/parlib.h>
#include <parlib/vcore.h>

__BEGIN_DECLS

#define I_AM_HERE printf("Vcore %d is in %s() at %s:%d\n", vcore_id(), \
                         __FUNCTION__, __FILE__, __LINE__);

/* For a poor-mans function tracer (can add these with spatch) */
void __print_func_entry(const char *func, const char *file);
void __print_func_exit(const char *func, const char *file);
#define print_func_entry() __print_func_entry(__FUNCTION__, __FILE__)
#define print_func_exit() __print_func_exit(__FUNCTION__, __FILE__)

/* user/parlib/hexdump.c */
void hexdump(FILE *f, void *v, int length);

__END_DECLS

#endif /* PARLIB_ROS_DEBUG_H */
