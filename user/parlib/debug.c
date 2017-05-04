#include <parlib/common.h>
#include <parlib/assert.h>
#include <parlib/stdio.h>
#include <parlib/parlib.h>
#include <stdio.h>
#include <unistd.h>
#include <parlib/spinlock.h>
#include <ros/common.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* This is called from glibc in delicate places, like signal handlers.  We might
 * as well just write any valid output to FD 2. */
int akaros_printf(const char *format, ...)
{
	char buf[128];
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	if (ret < 0)
		return ret;
	write(2, buf, MIN(sizeof(buf), ret));
	return ret;
}

/* Poor man's Ftrace, won't work well with concurrency. */
static const char *blacklist[] = {
	"whatever",
};

static bool is_blacklisted(const char *s)
{
	for (int i = 0; i < COUNT_OF(blacklist); i++) {
		if (!strcmp(blacklist[i], s))
			return TRUE;
	}
	return FALSE;
}

static int tab_depth = 0;
static bool print = TRUE;

void reset_print_func_depth(void)
{
	tab_depth = 0;
}

void toggle_print_func(void)
{
	print = !print;
	printf("Func entry/exit printing is now %sabled\n", print ? "en" : "dis");
}

static spinlock_t lock = {0};

void __print_func_entry(const char *func, const char *file)
{
	if (!print)
		return;
	if (is_blacklisted(func))
		return;
	spinlock_lock(&lock);
	printd("Vcore %2d", vcore_id());	/* helps with multicore output */
	for (int i = 0; i < tab_depth; i++)
		printf("\t");
	printf("%s() in %s\n", func, file);
	spinlock_unlock(&lock);
	tab_depth++;
}

void __print_func_exit(const char *func, const char *file)
{
	if (!print)
		return;
	if (is_blacklisted(func))
		return;
	tab_depth--;
	spinlock_lock(&lock);
	printd("Vcore %2d", vcore_id());
	for (int i = 0; i < tab_depth; i++)
		printf("\t");
	printf("---- %s()\n", func);
	spinlock_unlock(&lock);
}

static int kptrace;

static void trace_init(void *arg)
{
	kptrace = open("#kprof/kptrace", O_WRITE);
	if (kptrace < 0)
		perror("Unable to open kptrace!\n");
}

void trace_printf(const char *fmt, ...)
{
	va_list args;
	char buf[128];
	int amt;
	static parlib_once_t once = PARLIB_ONCE_INIT;

	parlib_run_once(&once, trace_init, NULL);
	if (kptrace < 0)
		return;
	amt = snprintf(buf, sizeof(buf), "PID %d: ", getpid());
	/* amt could be > sizeof, if we truncated. */
	amt = MIN(amt, sizeof(buf));
	va_start(args, fmt);
	/* amt == sizeof is OK here */
	amt += vsnprintf(buf + amt, sizeof(buf) - amt, fmt, args);
	va_end(args);
	write(kptrace, buf, MIN(amt, sizeof(buf)));
}
