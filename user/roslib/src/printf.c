// Implementation of cprintf console output for user processes,
// based on printfmt() and the sys_cputs() system call.
//
// cprintf is a debugging statement, not a generic output statement.
// It is very important that it always go to the console, especially when
// debugging file descriptor code!

#include <types.h>
#include <stdio.h>
#include <stdarg.h>
#include <lib.h>


// Collect up to BUF_SIZE characters into a buffer
// and perform ONE system call to print all of them,
// in order to make the lines output to the console atomic
// and prevent interrupts from causing context switches
// in the middle of a console output line and such.
#define BUF_SIZE 256
typedef struct printbuf {
	int idx;	// current buffer index
	int cnt;	// total bytes printed so far
	char buf[BUF_SIZE];
} printbuf_t;


static void putch(int ch, printbuf_t **b)
{
	(*b)->buf[(*b)->idx++] = ch;
	if ((*b)->idx == BUF_SIZE) {
		sys_cputs((*b)->buf, (*b)->idx);
		(*b)->idx = 0;
	}
	(*b)->cnt++;
}

int vcprintf(const char *fmt, va_list ap)
{
	printbuf_t b;
	printbuf_t *COUNT(1) bp = &b;

	b.idx = 0;
	b.cnt = 0;
	vprintfmt(putch, &bp, fmt, ap);
	sys_cputs(b.buf, b.idx);

	return b.cnt;
}

int cprintf(const char *fmt, ...)
{
	va_list ap;
	int cnt;

	va_start(ap, fmt);
	cnt = vcprintf(fmt, ap);
	va_end(ap);

	return cnt;
}

// Temp async varieties
#define MAX_BUFFERS 100
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
}

// This is called when the syscall is waited on
static void cputs_async_cleanup(void* data)
{
	POOL_PUT(&print_buf_pool, (printbuf_t*)data);
}

// TODO: its a little difficult to pass back an error through vprintfmt
static void putch_async(int ch, printbuf_t **b)
{
	(*b)->buf[(*b)->idx++] = ch;
	if ((*b)->idx == BUF_SIZE) {
		// TODO - should check for a return value for sys_ and get_sys
		sys_cputs_async((*b)->buf, (*b)->idx, get_sys_desc(current_async_desc),
		                cputs_async_cleanup, *b);
		// TODO - should check for a return value
		*b = get_free_buffer();
		(*b)->idx = 0;
	}
	(*b)->cnt++; // supposed to be overall number, not just in one buffer
}

static int vcprintf_async(const char *NTS fmt, va_list ap)
{
	// start with an available buffer.  TODO: check return value
	printbuf_t* b = get_free_buffer();

	b->idx = 0;
	b->cnt = 0;
	vprintfmt(putch_async, &b, fmt, ap);
	// TODO - should check for a return value for sys_
	sys_cputs_async(b->buf, b->idx, get_sys_desc(current_async_desc),
	                cputs_async_cleanup, b);

	return b->cnt; // this is lying if we used more than one buffer (TODO)
}

int cprintf_async(async_desc_t** desc, const char *fmt, ...)
{
	va_list ap;
	int cnt;

	// This async call has some housekeeping it needs to do once, ever.
	static bool initialized = 0;
	if (!initialized) {
		init_printf();
    	initialized = TRUE;
	}
	// get a free async_desc for this async call, and save it in the per-thread
	// tracking variable (current_async_desc).  then pass it back out.
	// TODO: check return value, return error_t
	current_async_desc = get_async_desc();
	*desc = current_async_desc;
	// This is the traditional (sync) cprintf code
	va_start(ap, fmt);
	cnt = vcprintf_async(fmt, ap);
	va_end(ap);

	// TODO: return cnt via a parameter, and return an error_t?
	return cnt;
}

