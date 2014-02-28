// Stripped-down primitive printf-style formatting routines,
// used in common by printf, sprintf, fprintf, etc.
// This code is also used by both the kernel and user programs.

#ifdef __SHARC__
#pragma nosharc
#endif

#include <ros/common.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <kthread.h>
#include <ns.h>
#include <acpi.h>

/* Print a number (base <= 16) in reverse order,
 * using specified putch function and associated pointer putdat. */
void printnum(void (*putch)(int, void**), void **putdat,
              unsigned long long num, unsigned base, int width, int padc)
{
	unsigned long long temp = num;
	int nr_digits = 1;
	/* Determine how many leading zeros we need.
	 * For every digit/nibble beyond base, we do one less width padding */
	while ((temp /= base)) {
		nr_digits++;
		width--;
	}
	/* And another one less, since we'll always print the last digit */
	while (--width > 0)
		putch(padc, putdat);
	for (int i = nr_digits; i > 0; i--) {
		temp = num;
		/* To get digit i, we only div (i-1) times */
		for (int j = 0; j < i - 1; j++) {
			temp /= base;
		}
		putch("0123456789abcdef"[temp % base], putdat);
	}
}

// Main function to format and print a string.
#ifdef __DEPUTY__
void printfmt(void (*putch)(int, TV(t)), TV(t) putdat, const char *fmt, ...);
#else
void printfmt(void (*putch)(int, void**), void **putdat, const char *fmt, ...);
#endif

#ifdef __DEPUTY__
void vprintfmt(void (*putch)(int, TV(t)), TV(t) putdat, const char *fmt, va_list ap)
#else
void vprintfmt(void (*putch)(int, void**), void **putdat, const char *fmt, va_list ap)
#endif
{
	register const char *NTS p;
	const char *NTS last_fmt;
	register int ch, err;
	unsigned long long num;
	int base, lflag, width, precision, altflag;
	char padc;
	uint8_t *mac, *ip, *mask;
	struct Gas *g;
	int i;
	uint32_t *lp;

	while (1) {
		while ((ch = *(unsigned char *) fmt) != '%') {
			if (ch == '\0')
				return;
			fmt++;
			putch(ch, putdat);
		}
		fmt++;

		// Process a %-escape sequence
		last_fmt = fmt;
		padc = ' ';
		width = -1;
		precision = -1;
		lflag = 0;
		altflag = 0;
	reswitch:
		switch (ch = *(unsigned char *) fmt++) {

		// flag to pad on the right
		case '-':
			padc = '-';
			goto reswitch;

		// flag to pad with 0's instead of spaces
		case '0':
			padc = '0';
			goto reswitch;

		// width field
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			for (precision = 0; ; ++fmt) {
				precision = precision * 10 + ch - '0';
				ch = *fmt;
				if (ch < '0' || ch > '9')
					break;
			}
			goto process_precision;

		case '*':
			precision = va_arg(ap, int);
			goto process_precision;

		case '.':
			if (width < 0)
				width = 0;
			goto reswitch;

		case '#':
			altflag = 1;
			goto reswitch;

		process_precision:
			if (width < 0)
				width = precision, precision = -1;
			goto reswitch;

		// long flag (doubled for long long)
		case 'l':
			lflag++;
			goto reswitch;

		// character
		case 'c':
			putch(va_arg(ap, int), putdat);
			break;

		// error message
		case 'e':
			err = va_arg(ap, int);
			if (err < 0)
				err = -err;
			if (err >= NUMERRORS)
				printfmt(putch, putdat, "error %d", err);
			else
				printfmt(putch, putdat, "%s", error_string[err]);
			break;

		case 'E': // ENET MAC
			if ((mac = va_arg(ap, uint8_t *)) == NULL){
				char *s = "00:00:00:00:00:00";
				while(*s)
					putch(*s++, putdat);
			}
			printemac(putch, putdat, mac);
			break;
		case 'i':
			/* what to do if they screw up? */
			if ((lp = va_arg(ap, uint32_t *)) != NULL){
				uint32_t hostfmt;
				for(i = 0; i < 4; i++){
					hnputl(&hostfmt, lp[i]);
					printfmt(putch, putdat, "%08lx", hostfmt);
				}
			}
			break;
		case 'I':
			/* what to do if they screw up? */
			if ((ip = va_arg(ap, uint8_t *)) != NULL)
				printip(putch, putdat, ip);
			break;
		case 'M':
			/* what to do if they screw up? */
			if ((mask = va_arg(ap, uint8_t *)) != NULL)
				printipmask(putch, putdat, mask);
			break;
		case 'V':
			/* what to do if they screw up? */
			if ((ip = va_arg(ap, uint8_t *)) != NULL)
				printipv4(putch, putdat, ip);
			break;

		// string
		case 's':
			if ((p = va_arg(ap, char *)) == NULL)
				p = "(null)";
			if (width > 0 && padc != '-')
				for (width -= strnlen(p, precision); width > 0; width--)
					putch(padc, putdat);
			for (; (ch = *p) != '\0' && (precision < 0 || --precision >= 0); width--) {
				if (altflag && (ch < ' ' || ch > '~'))
					putch('?', putdat);
				else
					putch(ch, putdat);
				// zra: make sure *p isn't '\0' before inc'ing
				p++;
			}
			for (; width > 0; width--)
				putch(' ', putdat);
			break;

		case 'd': /* (signed) decimal */
			if (lflag >= 2)
				num = va_arg(ap, long long);
			else if (lflag)
				num = va_arg(ap, long);
			else
				num = va_arg(ap, int);
			if ((long long) num < 0) {
				putch('-', putdat);
				num = -(long long) num;
			}
			base = 10;
			goto number;

		case 'u': /* unsigned decimal */
		case 'o': /* (unsigned) octal */
		case 'x': /* (unsigned) hexadecimal */
			if (lflag >= 2)
				num = va_arg(ap, unsigned long long);
			else if (lflag)
				num = va_arg(ap, unsigned long);
			else
				num = va_arg(ap, unsigned int);
			if (ch == 'u')
				base = 10;
			else if (ch == 'o')
				base = 8;
			else	/* x */
				base = 16;
			goto number;

		// pointer
		case 'p':
			putch('0', putdat);
			putch('x', putdat);
			/* automatically zero-pad pointers, out to the length of a ptr */
			padc = '0';
			width = sizeof(void*) * 2;	/* 8 bits per byte / 4 bits per char */
			num = (unsigned long long)
				(uintptr_t) va_arg(ap, void *);
			base = 16;
			goto number;

		number:
			printnum(putch, putdat, num, base, width, padc);
			break;

		// escaped '%' character
		case '%':
			putch(ch, putdat);
			break;

		// unrecognized escape sequence - just print it literally
		default:
			putch('%', putdat);
			fmt = last_fmt;
			//for (fmt--; fmt[-1] != '%'; fmt--)
				/* do nothing */;
			break;
		}
	}
}

#ifdef __DEPUTY__
void printfmt(void (*putch)(int, TV(t)), TV(t) putdat, const char *fmt, ...)
#else
void printfmt(void (*putch)(int, void**), void **putdat, const char *fmt, ...)
#endif
{
	va_list ap;

	va_start(ap, fmt);
	vprintfmt(putch, putdat, fmt, ap);
	va_end(ap);
	check_poison("printfmt");
}

typedef struct sprintbuf {
	char *BND(__this,ebuf) buf;
	char *SNT ebuf;
	int cnt;
} sprintbuf_t;

static void sprintputch(int ch, sprintbuf_t *NONNULL *NONNULL b)
{
	(*b)->cnt++;
	if ((*b)->buf < (*b)->ebuf)
		*((*b)->buf++) = ch;
}

int vsnprintf(char *buf, int n, const char *fmt, va_list ap)
{
	sprintbuf_t b;// = {buf, buf+n-1, 0};
	sprintbuf_t *COUNT(1) NONNULL bp = &b;

	if (buf == NULL || n < 1)
		return -EINVAL;

	b.buf = NULL; // zra : help out the Deputy optimizer a bit
	b.ebuf = buf+n-1;
	b.cnt = 0;
	b.buf = buf;

	// print the string to the buffer
	#ifdef __DEPUTY__
	vprintfmt((void*)sprintputch, (sprintbuf_t *NONNULL*NONNULL)&bp, fmt, ap);
	#else
	vprintfmt((void*)sprintputch, (void*)&bp, fmt, ap);
	#endif

	// null terminate the buffer
	*b.buf = '\0';

	return b.cnt;
}

int snprintf(char *buf, int n, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = vsnprintf(buf, n, fmt, ap);
	va_end(ap);

	check_poison("snprintf");
	return rc;
}

/* convenience function: do a print, return the pointer to the end. */
char *seprintf(char *buf, char *end, const char *fmt, ...)
{
	va_list ap;
	int rc;
	int n = end - buf;

	if (n <= 0)
		return buf;

	va_start(ap, fmt);
	rc = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	check_poison("seprintf");

	if (rc >= 0)
		return buf + rc;
	else
		return buf;
}
