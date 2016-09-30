#pragma once

#include <parlib/common.h>
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <unistd.h>

__BEGIN_DECLS

#define I_AM_HERE printf("Vcore %d is in %s() at %s:%d\n", vcore_id(), \
                         __FUNCTION__, __FILE__, __LINE__);

#define debug_printf(...) {                                                    \
	char buf[128];                                                             \
	int ret = snprintf(buf, sizeof(buf), __VA_ARGS__);                         \
	write(2, buf, ret);                                                        \
}
void trace_printf(const char *fmt, ...);

/* For a poor-mans function tracer (can add these with spatch) */
void __print_func_entry(const char *func, const char *file);
void __print_func_exit(const char *func, const char *file);
#define print_func_entry() __print_func_entry(__FUNCTION__, __FILE__)
#define print_func_exit() __print_func_exit(__FUNCTION__, __FILE__)

/* user/parlib/hexdump.c */
void hexdump(FILE *f, void *v, int length);

__END_DECLS
