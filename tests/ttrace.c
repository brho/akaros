// ttrace: basic functionality test of the #T ttrace device

#define _LARGEFILE64_SOURCE /* needed to use lseek64 */

#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <ros/ttrace.h>

#ifndef min
#define min(a, b) ({ \
	typeof (a) _a = (a); typeof (b) _b = (b); _a < _b ? _a : _b; })
#endif

typedef int Verbimpl(int argc, char *argv[]);

struct verbtab {
	const char *name;
	const char *abbrev;
	Verbimpl *impl;
	uint8_t min_args, max_args;
};

static const char *cmd_name;
static char mega_buffer[2 * 1024 * 1024];
static size_t mega_cur;
static size_t mega_remaining;

// Command implementation forward declarations
static int scaf_tswr(int argc, char *argv[]);
static int dump_data(int argc, char *argv[]);
static const struct verbtab verbs[] = {
	{ "scaf_tswr", "twr", &scaf_tswr, 2, 2 },
	{ "dump_data", "dd",  &dump_data, 0, 1 },
};
#define NVERBTAB (sizeof(verbs)/sizeof(verbs[0]))

static void mega_buffer_clear()
{
	mega_cur = 0;
	mega_remaining = sizeof(mega_buffer);
}

static void init(char *argv[])
{
	mega_buffer_clear();
	cmd_name = basename(argv[0]);
}

static void outerrf(const char *fmt, ...)
{
	fflush(stdout);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fflush(stderr);
}

static void outf(const char *fmt, ...)
{
	fflush(stderr);

	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

static void usage(const char *fmt, ...) __attribute__((noreturn));
static void usage(const char *fmt, ...)
{
	static const char usage_msg[] =
"<verb> [args...]\n"
"where <verb>:\n"
"    scaf_tswr(twr) <ts> <file>\n"
"        Scaffold write and read integer timestamp to given file.\n"
"    dump_data(data) [<ts>]\n"
"        Dump constant data table, from ts if given 1 otherwise.\n";

	va_list ap;
	char error_message[1024];

	/* Try to print in the allocated space */
	va_start(ap, fmt);
	vsnprintf(error_message, sizeof(error_message), fmt, ap);
	va_end(ap);

	outerrf("%s: %s\n%s %s",
			cmd_name, error_message, cmd_name, usage_msg);

	exit(EX_USAGE);
}

static void error(int code, int errnum, const char *fmt, ...)
	__attribute__((noreturn));
static void error(int code, int errnum, const char *fmt, ...)
{
	va_list ap;
	char error_message[1024];

	/* Try to print in the allocated space */
	va_start(ap, fmt);
	vsnprintf(error_message, sizeof(error_message), fmt, ap);
	va_end(ap);

	if (errnum)
		outerrf("%s: %s : %s(%d)\n",
				cmd_name, error_message, strerror(errnum), errnum);
	else
		outerrf("%s: %s\n", cmd_name, error_message);
	exit(code);
}

static int scaf_tswr(int argc, char *argv[])
{
	assert(2 == argc);
	const char * const arg_timestamp = argv[0];
	const char * const arg_fname = argv[1];
	char filename[10]; // big enough for "#T/cpunnn"

	size_t len = snprintf(filename, sizeof(filename), "#T/%s", arg_fname);
	if (len >= sizeof(filename))
		error(EX_DATAERR, 0,
			  "%filename '%s' too long for ttrace device", arg_fname);

	int fd = open(filename, O_RDWR);
	if (fd < 0)
		error(EX_NOINPUT, errno, "Can't open '%s'", filename);
	
	len = snprintf(mega_buffer, mega_remaining, "setts %s", arg_timestamp);
	if (write(fd, mega_buffer, len) != len)
		error(EX_DATAERR, errno, "Can't write command '%s'", mega_buffer);
	else
		outf("Wrote '%s' to %s\n", mega_buffer, filename);

	if (lseek64(fd, 0, SEEK_SET) < 0)
		error(EX_OSERR, errno, "Couldn't seek to beginning of '%s'", filename);

	len = read(fd, mega_buffer, mega_remaining - 1);
	if (len < 0)
		error(EX_OSERR, errno, "Can't read data from '%s'", filename);
	else {
		mega_buffer[len] = '\0';
		char *newline = strrchr(mega_buffer, '\n');
		if (newline) *newline = '\0';
		outf("Read '%s' from %s\n", mega_buffer, filename);
	}
	if (close(fd) < 0)
		error(EX_OSERR, errno, "Can't close '%s'", filename);

	return EX_OK;
}

static bool gethex4(char hexchar, uint8_t *outp)
{
	bool ok = false;
	if (isxdigit(hexchar)) {
		if (hexchar - '0' <= 9)
			*outp = hexchar - '0';
		else
			*outp = tolower(hexchar) - 'a' + 0xa;
		assert((unsigned) *outp < 0x10);
		ok = true;
	}
	return ok;
}

static bool _gethex8(const char *hexstr, uint8_t *outp)
{
	uint8_t hinib, lonib;
	bool ok = gethex4(hexstr[0], &hinib);
	if (ok)
		ok = gethex4(hexstr[1], &lonib);
	if (ok)
		*outp = hinib << 4 | lonib;
	return ok;
}

static bool _gethex16(const char *hexstr, uint16_t *outp)
{
	uint8_t hibyte, lobyte;
	bool ok = _gethex8(&hexstr[0], &hibyte)
		  &&  _gethex8(&hexstr[2], &lobyte);
	if (ok)
		*outp = ((uint16_t) hibyte << 8) | lobyte;
	return ok;
}

static bool _gethex32(const char *hexstr, uint32_t *outp)
{
	uint16_t hishort, loshort;
	bool ok = _gethex16(&hexstr[0], &hishort)
		  &&  _gethex16(&hexstr[4], &loshort);
	if (ok)
		*outp = ((uint32_t) hishort << 16) | loshort;
	return ok;
}

static bool _gethex64(const char *hexstr, uint64_t *outp)
{
	uint32_t hiint, loint;
	bool ok = _gethex32(&hexstr[0], &hiint)
		  &&  _gethex32(&hexstr[8], &loint);
	if (ok)
		*outp = ((uint64_t) hiint << 32) | loint;
	return ok;
}
  
static inline bool checklen(const char *str, size_t len)
{
	return strnlen(str, len) == len;
}

static inline bool gethex8(const char *hexstr, uint8_t *outp)
{
	return checklen(hexstr, 2) &&_gethex8(hexstr, outp);
}

static inline bool gethex16(const char *hexstr, uint16_t *outp)
{
	return checklen(hexstr, 4) &&_gethex16(hexstr, outp);
}

static inline bool gethex32(const char *hexstr, uint32_t *outp)
{
	return checklen(hexstr, 8) &&_gethex32(hexstr, outp);
}

static inline bool gethex64(const char *hexstr, uint64_t *outp)
{
	return checklen(hexstr, 16) && _gethex64(hexstr, outp);
}

static bool gethdr(const char *hexstr,
				   uint8_t *lenp, uint8_t *tagp, uint64_t *timestampp)
{
	bool ok = checklen(&hexstr[0], 4);
	ok = ok && _gethex8(&hexstr[0], lenp);
	ok = ok && _gethex8(&hexstr[2], tagp);
	assert(!ok || !(*tagp & 0x80)); // Can't deal with continuations yet
	ok = ok && gethex64(&hexstr[4], timestampp);
	return ok;
}

// Decode bsd nn.nn
static inline double decode_vers(uint16_t vers)
{
	return 10.00 * ((vers >> 12) & 0xf) + 1.00 * ((vers >>  8) & 0xf)
		  + 0.10 * ((vers >>  4) & 0xf) + 0.01 * ((vers)       & 0xf);
}

static void dd_decode_info(const char *line, int lineno)
{
	const char *info = &line[TTRACEH_LEN];
	uint16_t dv, cv, ncpu;
	bool ok = checklen(&line[TTRACEH_LEN], 12);
	ok = ok && _gethex16(&info[0], &dv);
	ok = ok && _gethex16(&info[4], &cv);
	ok = ok && _gethex16(&info[8], &ncpu);
	if (ok)
		outf("%5d: INFO data v%.02f cpu v%0.2f ncpu %d\n",
				lineno, decode_vers(dv), decode_vers(cv), ncpu);
	else
		outerrf("%5d: bad INFO %s\n", lineno, line);
}

static void dd_decode_sysc(const char *line, int lineno)
{
	uint64_t id;
	bool ok = gethex64(&line[TTRACEH_LEN], &id);
	if (id < (1 << 16))
		outf("%5d: SYSC %3lld %s\n", lineno, id, &line[16 + TTRACEH_LEN]);
	else
		outerrf("%5d: Bad sysc %016llx %s\n", lineno, id, line);

}

static void dd_decode_line(const char *line, int lineno)
{
	uint8_t len;
	uint8_t tag;
	uint64_t timestamp;
	bool ok = gethdr(line, &len, &tag, &timestamp);
	if (!ok) {
		outerrf("%5d: bad hdr %s\n", lineno, line);
		return;
	}
	if (strlen(line) != len) {
		outerrf(" %5d: bad len %s\n", lineno, line);
		return;
	}
	switch (tag) {
	case TTRACEH_TAG_INFO: dd_decode_info(line, lineno); break;
	case TTRACEH_TAG_SYSC: dd_decode_sysc(line, lineno); break;
	default:
		outerrf("bad tag %4d: %s\n", lineno, line);
	}
}

static void outerrline(const char *line, int lineno)
{
	outerrf("%5d: len %d\n", lineno, strlen(line));
	outerrf("     %-2.2s %-2.2s %-16.16s %s\n",
			&line[0], &line[2], &line[4], &line[20]);

}

static int dump_data(int argc, char *argv[])
{
	assert(argc <= 1);
	const char * const arg_timestamp = argv[0];
	char filename[] = "#T/data";

	int fd = open(filename, O_RDWR);
	if (fd < 0)
		error(EX_NOINPUT, errno, "Can't open '%s'", filename);
	
	ssize_t len;
	if (argc == 1) {
		len = snprintf(mega_buffer, mega_remaining, "setts %s", arg_timestamp);
		if (write(fd, mega_buffer, len) != len)
			error(EX_DATAERR, errno, "Can't write command '%s'", mega_buffer);
		else
			outf("Wrote '%s' to %s\n", mega_buffer, filename);
	}

	mega_buffer_clear();
	len = read(fd, mega_buffer, mega_remaining - 1);
	if (len < 0)
		error(EX_OSERR, errno, "Can't read data from '%s'", filename);
	else {
		mega_buffer[len] = '\0';
		mega_remaining -= len;
	}

	int nlines = 0;
	char *cp = mega_buffer;
	while ( (cp = strchr(cp+1, '\n')) )
		nlines++;
	outf("Read %d data lines for %d bytes\n", nlines, len);
	char *line, *saveptr = NULL;
	int lineno = 0;
	for (cp = mega_buffer; (line = strtok_r(cp, "\n", &saveptr)); cp = NULL) {
		dd_decode_line(line, ++lineno);
	}

	if (close(fd) < 0)
		error(EX_OSERR, errno, "Can't close '%s'", filename);

	return EX_OK;
}

int main(int argc, char *argv[])
{
	init(argv);

	if (argc < 2) usage("No verb");
	argc -= 2;

    const char *verb_str = argv[1];
	memmove(&argv[0], &argv[2], argc * sizeof(argv[0]));
	for (int i = 0; i < NVERBTAB; i++) {
		const struct verbtab * const cmdp = &verbs[i];
		if (strcmp(verb_str, cmdp->name) && strcmp(verb_str, cmdp->abbrev))
			continue;
		if (argc < cmdp->min_args)
			usage("verb '%s': requires %d arguments, %d given",
				  cmdp->name, cmdp->min_args, argc);
		else if (cmdp->max_args > 0 && cmdp->max_args < argc)
			usage("verb '%s': requires at most %d arguments, %d given",
				  cmdp->name, cmdp->min_args, argc);

		// Found a valid command, call it and exit
		return (*cmdp->impl)(argc, argv);
	}
	usage("verb '%s': not recognised", verb_str);
}
