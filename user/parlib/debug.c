#include <parlib/common.h>
#include <parlib/parlib.h>
#include <stdio.h>
#include <unistd.h>
#include <parlib/spinlock.h>

int akaros_printf(const char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = vprintf(format, ap);
	va_end(ap);
	return ret;
}

/* Poor man's Ftrace, won't work well with concurrency. */
static const char *blacklist[] = {
	"whatever",
};

static bool is_blacklisted(const char *s)
{
	#define ARRAY_SIZE(x) (sizeof((x))/sizeof((x)[0]))
	for (int i = 0; i < ARRAY_SIZE(blacklist); i++) {
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
