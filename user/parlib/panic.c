#include <parlib/arch/arch.h>
#include <stdio.h>
#include <stdarg.h>
#include <parlib/assert.h>
#include <stdlib.h>
#include <parlib/ros_debug.h>

char *argv0;

static void __attribute__((constructor)) parlib_stdio_init(void)
{
	/* This isn't ideal, since it might affect some stdout streams where our
	 * parent tried to do something else.  Note that isatty() always returns
	 * TRUE, due to how we fake tcgetattr(), and that doesn't affect whatever
	 * our shells are doing to set us up. */
	setlinebuf(stdout);
}

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
	printf("[user] %s:%d, vcore %d, Assertion failed: %s\n", file, line,
	       vcore_id(), msg);
	abort();
}
