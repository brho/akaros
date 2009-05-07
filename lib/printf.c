// Implementation of cprintf console output for user environments,
// based on printfmt() and the sys_cputs() system call.
//
// cprintf is a debugging statement, not a generic output statement.
// It is very important that it always go to the console, especially when 
// debugging file descriptor code!

#include <inc/types.h>
#include <inc/stdio.h>
#include <inc/stdarg.h>
#include <inc/lib.h>


// Collect up to 256 characters into a buffer
// and perform ONE system call to print all of them,
// in order to make the lines output to the console atomic
// and prevent interrupts from causing context switches
// in the middle of a console output line and such.
typedef struct printbuf {
	int idx;	// current buffer index
	int cnt;	// total bytes printed so far
	char buf[256];
} printbuf_t;


static void
putch(int ch, printbuf_t *b)
{
	b->buf[b->idx++] = ch;
	if (b->idx == 256-1) {
		sys_cputs(b->buf, b->idx);
		b->idx = 0;
	}
	b->cnt++;
}

int
vcprintf(const char *fmt, va_list ap)
{
	printbuf_t b;

	b.idx = 0;
	b.cnt = 0;
	vprintfmt((void*)putch, &b, fmt, ap);
	sys_cputs(b.buf, b.idx);

	return b.cnt;
}

int
cprintf(const char *fmt, ...)
{
	va_list ap;
	int cnt;

	va_start(ap, fmt);
	cnt = vcprintf(fmt, ap);
	va_end(ap);

	return cnt;
}

// Temp async varieties
#define MAX_BUFFERS 10
POOL_TYPE_DEFINE(printbuf_t, print_buf_pool, MAX_BUFFERS);
print_buf_pool_t print_buf_pool;

static error_t init_printf(void)
{
	POOL_INIT(&print_buf_pool, MAX_BUFFERS);
	return 0;
}

static printbuf_t* get_free_buffer(void)
{
	return POOL_GET(&print_buf_pool);
	//cprintf("Out of buffers!!!\n");
}

// this buffering is a minor pain in the ass....
// TODO - it will be a pain in the ass to put the buffers back after we waited.
static void putch_async(int ch, printbuf_t *b)
{
	b->buf[b->idx++] = ch;
	if (b->idx == 256-1) {
		// will need some way to track the result of the syscall
		sys_cputs_async(b->buf, b->idx, get_sys_desc(current_async_desc));
		// TODO - this isn't getting passed back properly
		b = get_free_buffer();
		b->idx = 0;
	}
	b->cnt++; // supposed to be overall number, not just in one buffer
}

static int vcprintf_async(const char *fmt, va_list ap)
{
	// start with an available buffer
	printbuf_t* b = get_free_buffer();

	b->idx = 0;
	b->cnt = 0;
	vprintfmt((void*)putch_async, b, fmt, ap);
	sys_cputs_async(b->buf, b->idx, get_sys_desc(current_async_desc));

	return b->cnt; // this is lying if we used more than one buffer
}

int cprintf_async(async_desc_t** desc, const char *fmt, ...)
{
	va_list ap;
	int cnt;
	static bool initialized = 0;
	if (!initialized) {
		init_printf();
    	initialized = TRUE;
	}
	current_async_desc = get_async_desc();
	*desc = current_async_desc;

	va_start(ap, fmt);
	cnt = vcprintf_async(fmt, ap);
	va_end(ap);

	return cnt;
}

