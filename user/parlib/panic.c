#include <parlib/arch/arch.h>
#include <parlib/stdio.h>
#include <parlib/assert.h>
#include <parlib/ros_debug.h>
#include <stdarg.h>
#include <stdlib.h>

static void __attribute__((constructor)) parlib_stdio_ctor(void)
{
	if (__in_fake_parlib())
		return;
	/* This isn't ideal, since it might affect some stdout streams where our
	 * parent tried to do something else.  Note that isatty() always returns
	 * TRUE, due to how we fake tcgetattr(), and that doesn't affect whatever
	 * our shells are doing to set us up. */
	setlinebuf(stdout);
}

static void __attribute__((noreturn)) fatal_backtrace(void)
{
	/* This will cause the kernel to print out a backtrace to the console.
	 * Short of reading /proc/self/maps or other stuff, userspace would have a
	 * hard time backtracing itself. */
	breakpoint();
	abort();
}

void _panic(const char *file, int line, const char *fmt, ...)
{
	char buf[128];
	int ret = 0;
	va_list ap;

	va_start(ap, fmt);
	ret += snprintf(buf + ret, sizeof(buf) - ret,
	                "[user] panic: PID %d, vcore %d, %s:%d: ",
	                getpid(), vcore_id(), file, line);
	/* ignore errors (ret < 0) by setting ret to be at least 0 */
	ret = MAX(ret, 0);
	ret += vsnprintf(buf + ret, sizeof(buf) - ret, fmt, ap);
	ret = MAX(ret, 0);
	ret += snprintf(buf + ret, sizeof(buf) - ret, "\n");
	ret = MAX(ret, 0);
	write(2, buf, ret);
	fatal_backtrace();
}

void _assert_failed(const char *file, int line, const char *msg)
{
	fprintf(stderr, "[user] %s:%d, vcore %d, Assertion failed: %s\n", file,
	        line, vcore_id(), msg);
	fatal_backtrace();
}
