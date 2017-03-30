/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #vars device, exports read access to select kernel variables.  These
 * variables are statically set.
 *
 * To add a variable, add a DEVVARS_ENTRY(name, format) somewhere in the kernel.
 * The format is a string consisting of two characters, using a modified version
 * of QEMU's formatting rules (ignoring count): [data_format][size]
 *
 * data_format is:
 *     x (hex)
 *     d (decimal)
 *     u (unsigned)
 *     o (octal)
 *     c (char)				does not need a size
 *     s (string)			does not need a size
 * size is:
 *     b (8 bits)
 *     h (16 bits)
 *     w (32 bits)
 *     g (64 bits)
 *
 * e.g. DEVVARS_ENTRY(num_cores, "dw");
 *
 * Another thing we can consider doing is implementing create() to add variables
 * on the fly.  We can easily get the address (symbol table), but not the type,
 * unless we get debugging info.  We could consider a CTL command to allow the
 * user to change the type, though that might overload write() if we also allow
 * setting variables. */

#include <ns.h>
#include <kmalloc.h>
#include <kref.h>
#include <atomic.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <sys/queue.h>
#include <fdtap.h>
#include <syscall.h>

struct dev vars_devtab;

static char *devname(void)
{
	return vars_devtab.name;
}

static struct dirtab *vars_dir;
static size_t nr_vars;
static qlock_t vars_lock;

struct dirtab __attribute__((__section__("devvars")))
              __devvars_dot = {".", {0, 0, QTDIR}, 0, DMDIR | 0555};

DEVVARS_ENTRY(num_cores, "dw");

static bool var_is_valid(struct dirtab *dir)
{
	return dir->qid.vers != -1;
}

/* Careful with this.  c->name->s is the full path, at least sometimes. */
static struct dirtab *find_var_by_name(const char *name)
{
	for (size_t i = 0; i < nr_vars; i++)
		if (!strcmp(vars_dir[i].name, name))
			return &vars_dir[i];
	return 0;
}

static void vars_init(void)
{
	/* If you name a section without a '.', GCC will create start and stop
	 * symbols, e.g. __start_SECTION */
	extern struct dirtab __start_devvars;
	extern struct dirtab __stop_devvars;
	struct dirtab *dot, temp;

	nr_vars = &__stop_devvars - &__start_devvars;
	vars_dir = kmalloc_array(nr_vars, sizeof(struct dirtab), MEM_WAIT);
	if (!vars_dir)
		error(ENOMEM, "kmalloc_array failed, nr_vars was %p", nr_vars);
	memcpy(vars_dir, &__start_devvars, nr_vars * sizeof(struct dirtab));
	/* "." needs to be the first entry in a devtable.  It might already be
	 * first, but we can do the swap regardless. */
	temp = vars_dir[0];
	dot = find_var_by_name(".");
	assert(dot);
	vars_dir[0] = *dot;
	*dot = temp;
	qlock_init(&vars_lock);
}

static struct chan *vars_attach(char *spec)
{
	struct chan *c;

	c = devattach(devname(), spec);
	mkqid(&c->qid, 0, 0, QTDIR);
	return c;
}

static struct walkqid *vars_walk(struct chan *c, struct chan *nc, char **name,
								 int nname)
{
	ERRSTACK(1);
	struct walkqid *ret;

	qlock(&vars_lock);
	if (waserror()) {
		qunlock(&vars_lock);
		nexterror();
	}
	ret = devwalk(c, nc, name, nname, vars_dir, nr_vars, devgen);
	poperror();
	qunlock(&vars_lock);
	return ret;
}

static int vars_stat(struct chan *c, uint8_t *db, int n)
{
	ERRSTACK(1);
	int ret;

	qlock(&vars_lock);
	if (waserror()) {
		qunlock(&vars_lock);
		nexterror();
	}
	ret = devstat(c, db, n, vars_dir, nr_vars, devgen);
	poperror();
	qunlock(&vars_lock);
	return ret;
}

static struct chan *vars_open(struct chan *c, int omode)
{
	ERRSTACK(1);
	struct chan *ret;

	qlock(&vars_lock);
	if (waserror()) {
		qunlock(&vars_lock);
		nexterror();
	}
	ret = devopen(c, omode, vars_dir, nr_vars, devgen);
	poperror();
	qunlock(&vars_lock);
	return ret;
}

static void vars_close(struct chan *c)
{
}

static struct dirtab *find_free_var(void)
{
	for (size_t i = 0; i < nr_vars; i++)
		if (!var_is_valid(&vars_dir[i]))
			return &vars_dir[i];
	return 0;
}

/* We ignore the perm - they are all hard-coded in the dirtab */
static void vars_create(struct chan *c, char *name, int omode, uint32_t perm)
{
	struct dirtab *new_slot;
	uintptr_t addr;
	char *bang;
	size_t size;

	/* TODO: check that the user is privileged */
	bang = strchr(name, '!');
	if (!bang)
		error(EINVAL, "Var %s has no ! in its format string", name);
	*bang = 0;
	addr = get_symbol_addr(name);
	*bang = '!';
	if (!addr)
		error(EINVAL, "Could not find symbol for %s", name);
	bang++;
	/* Note that we don't check the symbol type against the format.  We're
	 * trusting the user here.  o/w we'd need dwarf support. */
	switch (*bang) {
	case 'c':
		size = sizeof(char);
		break;
	case 's':
		size = sizeof(char*);
		break;
	case 'd':
	case 'x':
	case 'u':
	case 'o':
		bang++;
		switch (*bang) {
		case 'b':
			size = sizeof(uint8_t);
			break;
		case 'h':
			size = sizeof(uint16_t);
			break;
		case 'w':
			size = sizeof(uint32_t);
			break;
		case 'g':
			size = sizeof(uint64_t);
			break;
		default:
			error(EINVAL, "Bad var size '%c'", *bang);
		}
		break;
	default:
		error(EINVAL, "Unknown var data_format '%c'", *bang);
	}
	bang++;
	if (*bang)
		error(EINVAL, "Extra chars for var %s", name);

	qlock(&vars_lock);
	new_slot = find_free_var();
	if (!new_slot) {
		vars_dir = kreallocarray(vars_dir, nr_vars * 2, sizeof(struct dirtab),
		                         MEM_WAIT);
		if (!vars_dir)
			error(ENOMEM, "krealloc_array failed, nr_vars was %p", nr_vars);
		memset(vars_dir + nr_vars, 0, nr_vars * sizeof(struct dirtab));
		for (size_t i = nr_vars; i < nr_vars * 2; i++)
			vars_dir[i].qid.vers = -1;
		new_slot = vars_dir + nr_vars;
		nr_vars *= 2;
	}
	strlcpy(new_slot->name, name, sizeof(new_slot->name));
	new_slot->qid.path = addr;
	new_slot->qid.vers = 0;
	new_slot->qid.type = QTFILE;
	new_slot->length = size;
	new_slot->perm = 0444;
	c->qid = new_slot->qid;		/* need to update c with its new qid */
	qunlock(&vars_lock);
	c->mode = openmode(omode);
}

static const char *get_integer_fmt(char data_fmt, char data_size)
{
	switch (data_fmt) {
	case 'x':
		switch (data_size) {
		case 'b':
		case 'h':
		case 'w':
			return "0x%x";
		case 'g':
			return "0x%lx";
		}
	case 'd':
		switch (data_size) {
		case 'b':
		case 'h':
		case 'w':
			return "%d";
		case 'g':
			return "%ld";
		}
	case 'u':
		switch (data_size) {
		case 'b':
		case 'h':
		case 'w':
			return "%u";
		case 'g':
			return "%lu";
		}
	case 'o':
		switch (data_size) {
		case 'b':
		case 'h':
		case 'w':
			return "0%o";
		case 'g':
			return "0%lo";
		}
	}
	return 0;
}

static long vars_read(struct chan *c, void *ubuf, long n, int64_t offset)
{
	ERRSTACK(1);
	char tmp[128];	/* big enough for any number and most strings */
	size_t size = sizeof(tmp);
	char data_size, data_fmt, *fmt;
	const char *fmt_int;
	bool is_signed = FALSE;
	long ret;

	qlock(&vars_lock);
	if (waserror()) {
		qunlock(&vars_lock);
		nexterror();
	}

	if (c->qid.type == QTDIR) {
		ret = devdirread(c, ubuf, n, vars_dir, nr_vars, devgen);
		poperror();
		qunlock(&vars_lock);
		return ret;
	}

	/* These checks are mostly for the static variables.  They are a
	 * double-check for the user-provided vars. */
	fmt = strchr(c->name->s, '!');
	if (!fmt)
		error(EINVAL, "var %s has no ! in its format string", c->name->s);
	fmt++;
	data_fmt = *fmt;
	if (!data_fmt)
		error(EINVAL, "var %s has no data_format", c->name->s);

	switch (data_fmt) {
	case 'c':
		size = snprintf(tmp, size, "%c", *(char*)c->qid.path);
		break;
	case 's':
		size = snprintf(tmp, size, "%s", *(char**)c->qid.path);
		break;
	case 'd':
		is_signed = TRUE;
		/* fall through */
	case 'x':
	case 'u':
	case 'o':
		fmt++;
		data_size = *fmt;
		if (!data_size)
			error(EINVAL, "var %s has no size", c->name->s);
		fmt_int = get_integer_fmt(data_fmt, data_size);
		if (!fmt_int)
			error(EINVAL, "#%s was unable to get an int fmt for %s",
			      devname(), c->name->s);
		switch (data_size) {
		case 'b':
			if (is_signed)
				size = snprintf(tmp, size, fmt_int, *(int8_t*)c->qid.path);
			else
				size = snprintf(tmp, size, fmt_int, *(uint8_t*)c->qid.path);
			break;
		case 'h':
			if (is_signed)
				size = snprintf(tmp, size, fmt_int, *(int16_t*)c->qid.path);
			else
				size = snprintf(tmp, size, fmt_int, *(uint16_t*)c->qid.path);
			break;
		case 'w':
			if (is_signed)
				size = snprintf(tmp, size, fmt_int, *(int32_t*)c->qid.path);
			else
				size = snprintf(tmp, size, fmt_int, *(uint32_t*)c->qid.path);
			break;
		case 'g':
			if (is_signed)
				size = snprintf(tmp, size, fmt_int, *(int64_t*)c->qid.path);
			else
				size = snprintf(tmp, size, fmt_int, *(uint64_t*)c->qid.path);
			break;
		default:
			error(EINVAL, "Bad #%s size %c", devname(), data_size);
		}
		break;
	default:
		error(EINVAL, "Unknown #%s data_format %c", devname(), data_fmt);
	}
	fmt++;
	if (*fmt)
		error(EINVAL, "Extra characters after var %s", c->name->s);
	ret = readmem(offset, ubuf, n, tmp, size + 1);
	poperror();
	qunlock(&vars_lock);
	return ret;
}

static long vars_write(struct chan *c, void *ubuf, long n, int64_t offset)
{
	error(EFAIL, "Can't write to a #%s file", devname());
}

/* remove is interesting.  we mark the qid in the dirtab as -1, which is a
 * signal to devgen that it is an invalid entry.  someone could already have
 * done a walk (before we qlocked) and grabbed the qid before it was -1.  as far
 * as they are concerned, they have a valid entry, since "the qid is the file"
 * for devvars.  (i.e. the chan gets a copy of the entire file, which fits into
 * the qid). */
static void vars_remove(struct chan *c)
{
	ERRSTACK(1);
	struct dirtab *dir;
	char *dir_name;

	/* chan's name may have multiple elements in the path; get the last one. */
	dir_name = strrchr(c->name->s, '/');
	dir_name = dir_name ? dir_name + 1 : c->name->s;

	qlock(&vars_lock);
	if (waserror()) {
		qunlock(&vars_lock);
		nexterror();
	}
	dir = find_var_by_name(dir_name);
	if (!dir || dir->qid.vers == -1)
		error(ENOENT, "Failed to remove %s, was it already removed?",
		      c->name->s);
	dir->qid.vers = -1;
	poperror();
	qunlock(&vars_lock);
}

struct dev vars_devtab __devtab = {
	.name = "vars",
	.reset = devreset,
	.init = vars_init,
	.shutdown = devshutdown,
	.attach = vars_attach,
	.walk = vars_walk,
	.stat = vars_stat,
	.open = vars_open,
	.create = vars_create,
	.close = vars_close,
	.read = vars_read,
	.bread = devbread,
	.write = vars_write,
	.bwrite = devbwrite,
	.remove = vars_remove,
	.wstat = devwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
	.tapfd = 0,
};

/* The utest needs these variables exported */
#ifdef CONFIG_DEVVARS_TEST

static char *s = "string";
static char c = 'x';
static uint8_t  u8  = 8;
static uint16_t u16 = 16;
static uint32_t u32 = 32;
static uint64_t u64 = 64;
static uint8_t  d8  = -8;
static uint16_t d16 = -16;
static uint32_t d32 = -32;
static uint64_t d64 = -64;
static uint8_t  x8  = 0x8;
static uint16_t x16 = 0x16;
static uint32_t x32 = 0x32;
static uint64_t x64 = 0x64;
static uint8_t  o8  = 01;
static uint16_t o16 = 016;
static uint32_t o32 = 032;
static uint64_t o64 = 064;

/* For testing creation.  There is no ENTRY for this. */
char *devvars_foobar = "foobar";

DEVVARS_ENTRY(s, "s");
DEVVARS_ENTRY(c, "c");
DEVVARS_ENTRY(u8,  "ub");
DEVVARS_ENTRY(u16, "uh");
DEVVARS_ENTRY(u32, "uw");
DEVVARS_ENTRY(u64, "ug");
DEVVARS_ENTRY(d8,  "db");
DEVVARS_ENTRY(d16, "dh");
DEVVARS_ENTRY(d32, "dw");
DEVVARS_ENTRY(d64, "dg");
DEVVARS_ENTRY(x8,  "xb");
DEVVARS_ENTRY(x16, "xh");
DEVVARS_ENTRY(x32, "xw");
DEVVARS_ENTRY(x64, "xg");
DEVVARS_ENTRY(o8,  "ob");
DEVVARS_ENTRY(o16, "oh");
DEVVARS_ENTRY(o32, "ow");
DEVVARS_ENTRY(o64, "og");

#endif /* CONFIG_DEVVARS_TEST */
