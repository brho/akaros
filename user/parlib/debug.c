// Implementation of cprintf console output for user processes,
// based on printfmt() and the sys_cputs() system call.
//
// cprintf is a debugging statement, not a generic output statement.
// It is very important that it always go to the console, especially when
// debugging file descriptor code!

#include <ros/common.h>
#include <parlib.h>
#include <stdio.h>

// Collect up to BUF_SIZE characters into a buffer
// and perform ONE system call to print all of them,
// in order to make the lines output to the console atomic
// and prevent interrupts from causing context switches
// in the middle of a console output line and such.
#define BUF_SIZE 256
typedef struct debugbuf {
	size_t  idx;	// current buffer index
	size_t  cnt;	// total bytes printed so far
	uint8_t buf[BUF_SIZE];
} debugbuf_t;


static void putch(int ch, debugbuf_t **b)
{
	(*b)->buf[(*b)->idx++] = ch;
	if ((*b)->idx == BUF_SIZE) {
		sys_cputs((*b)->buf, (*b)->idx);
		(*b)->idx = 0;
	}
	(*b)->cnt++;
}

int ros_vdebug(const char *fmt, va_list ap)
{
	debugbuf_t b;
	debugbuf_t *COUNT(1) bp = &b;

	b.idx = 0;
	b.cnt = 0;
	ros_vdebugfmt((void*)putch, (void*)&bp, fmt, ap);
	sys_cputs(b.buf, b.idx);

	return b.cnt;
}

int ros_debug(const char *fmt, ...)
{
	va_list ap;
	int cnt;

	va_start(ap, fmt);
	cnt = ros_vdebug(fmt, ap);
	va_end(ap);

	return cnt;
}

