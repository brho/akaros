#include <parlib/arch/arch.h>
#include <stdio.h>
#include <stdarg.h>
#include <parlib/rassert.h>
#include <stdlib.h>
#include <parlib/ros_debug.h>

char *argv0;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: <message>", then causes a breakpoint exception,
 * which causes ROS to enter the ROS kernel monitor.
 */
void
_panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);

	// Print the panic message
	if (argv0)
		printf("%s: ", argv0);
	printf("user panic at %s:%d: ", file, line);
	vprintf(fmt, ap);
	printf("\n");

	// Cause a breakpoint exception
	while (1)
		breakpoint();
}

void _assert_failed(const char *file, int line, const char *msg)
{
	ros_debug("[user] %s:%d, vcore %d, Assertion failed: %s\n",
	          file, line, vcore_id(), msg);
	breakpoint();
	abort();
}
