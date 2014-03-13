/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <printf-ext.h>

#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <ctype.h>
#include <error.h>
#include <iplib.h>
#include <dir.h>
#include <ndb.h>
#include <fcall.h>

static int dumpsome(FILE *, char *, long);
static int fdirconv(FILE *, struct dir *);
static char *qidtype(char *, uint8_t);

#define	QIDFMT	"(%.16llux %lud %s)"

int printf_fcall(FILE * stream, const struct printf_info *info,
				 const void *const *args)
{
	struct fcall *f = *(void **)args[0];
	int fid, type, tag, i, retval = 0;
	char buf[512], tmp[200];
	char *p, *e;
	struct dir *d;
	struct qid *q;

	e = buf + sizeof(buf);
	type = f->type;
	fid = f->fid;
	tag = f->tag;
	switch (type) {
		case Tversion:	/* 100 */
			retval +=
				fprintf(stream, "Tversion tag %ud msize %ud version '%s'", tag,
						f->msize, f->version);
			break;
		case Rversion:
			retval +=
				fprintf(stream, "Rversion tag %ud msize %ud version '%s'", tag,
						f->msize, f->version);
			break;
		case Tauth:	/* 102 */
			retval +=
				fprintf(stream, "Tauth tag %ud afid %d uname %s aname %s", tag,
						f->afid, f->uname, f->aname);
			break;
		case Rauth:
			retval += fprintf(stream, "Rauth tag %ud qid " QIDFMT, tag,
							  f->aqid.path, f->aqid.vers, qidtype(tmp,
																  f->aqid.
																  type));
			break;
		case Tattach:	/* 104 */
			retval +=
				fprintf(stream,
						"Tattach tag %ud fid %d afid %d uname %s aname %s", tag,
						fid, f->afid, f->uname, f->aname);
			break;
		case Rattach:
			retval += fprintf(stream, "Rattach tag %ud qid " QIDFMT, tag,
							  f->qid.path, f->qid.vers, qidtype(tmp,
																f->qid.type));
			break;
		case Rerror:	/* 107; 106 (Terror) illegal */
			retval += fprintf(stream, "Rerror tag %ud ename %s", tag, f->ename);
			break;
		case Tflush:	/* 108 */
			retval +=
				fprintf(stream, "Tflush tag %ud oldtag %ud", tag, f->oldtag);
			break;
		case Rflush:
			retval += fprintf(stream, "Rflush tag %ud", tag);
			break;
		case Twalk:	/* 110 */
			retval +=
				fprintf(stream, "Twalk tag %ud fid %d newfid %d nwname %d ",
						tag, fid, f->newfid, f->nwname);
			if (f->nwname <= MAXWELEM)
				for (i = 0; i < f->nwname; i++)
					retval += fprintf(stream, "%d:%s ", i, f->wname[i]);
			break;
		case Rwalk:
			retval +=
				fprintf(stream, "Rwalk tag %ud nwqid %ud ", tag, f->nwqid);
			if (f->nwqid <= MAXWELEM)
				for (i = 0; i < f->nwqid; i++) {
					q = &f->wqid[i];
					retval += fprintf(stream, "%d:" QIDFMT " ", i,
									  q->path, q->vers, qidtype(tmp, q->type));
				}
			break;
		case Topen:	/* 112 */
			retval +=
				fprintf(stream, "Topen tag %ud fid %ud mode %d", tag, fid,
						f->mode);
			break;
		case Ropen:
			retval +=
				fprintf(stream, "Ropen tag %ud qid " QIDFMT " iounit %ud ", tag,
						f->qid.path, f->qid.vers, qidtype(tmp, f->qid.type),
						f->iounit);
			break;
		case Tcreate:	/* 114 */
			retval +=
				fprintf(stream,
						"Tcreate tag %ud fid %ud name %s perm %d mode %d", tag,
						fid, f->name, (uint32_t) f->perm, f->mode);
			break;
		case Rcreate:
			retval +=
				fprintf(stream, "Rcreate tag %ud qid " QIDFMT " iounit %ud ",
						tag, f->qid.path, f->qid.vers, qidtype(tmp,
															   f->qid.type),
						f->iounit);
			break;
		case Tread:	/* 116 */
			retval +=
				fprintf(stream, "Tread tag %ud fid %d offset %lld count %ud",
						tag, fid, f->offset, f->count);
			break;
		case Rread:
			retval +=
				fprintf(stream, "Rread tag %ud count %ud ", tag, f->count);
			retval += dumpsome(stream, f->data, f->count);
			break;
		case Twrite:	/* 118 */
			retval +=
				fprintf(stream, "Twrite tag %ud fid %d offset %lld count %ud ",
						tag, fid, f->offset, f->count);
			retval += dumpsome(stream, f->data, f->count);
			break;
		case Rwrite:
			retval +=
				fprintf(stream, "Rwrite tag %ud count %ud", tag, f->count);
			break;
		case Tclunk:	/* 120 */
			retval += fprintf(stream, "Tclunk tag %ud fid %ud", tag, fid);
			break;
		case Rclunk:
			retval += fprintf(stream, "Rclunk tag %ud", tag);
			break;
		case Tremove:	/* 122 */
			retval += fprintf(stream, "Tremove tag %ud fid %ud", tag, fid);
			break;
		case Rremove:
			retval += fprintf(stream, "Rremove tag %ud", tag);
			break;
		case Tstat:	/* 124 */
			retval += fprintf(stream, "Tstat tag %ud fid %ud", tag, fid);
			break;
		case Rstat:
			retval += fprintf(stream, "Rstat tag %ud ", tag);
			if (f->nstat > sizeof tmp)
				retval += fprintf(stream, " stat(%d bytes)", f->nstat);
			else {
				d = (struct dir *)tmp;
				convM2D(f->stat, f->nstat, d, (char *)(d + 1));
				retval += fprintf(stream, " stat ");
				retval += fdirconv(stream, d);
			}
			break;
		case Twstat:	/* 126 */
			retval += fprintf(stream, "Twstat tag %ud fid %ud", tag, fid);
			if (f->nstat > sizeof tmp)
				retval += fprintf(stream, " stat(%d bytes)", f->nstat);
			else {
				d = (struct dir *)tmp;
				convM2D(f->stat, f->nstat, d, (char *)(d + 1));
				retval += fprintf(stream, " stat ");
				retval += fdirconv(stream, d);
			}
			break;
		case Rwstat:
			retval += fprintf(stream, "Rwstat tag %ud", tag);
			break;
		default:
			retval += fprintf(stream, "unknown type %d", type);
	}
	return retval;
}

static char *qidtype(char *s, uint8_t t)
{
	char *p;
#define QTDIR              0x80	/* type bit for directories */
	p = s;
	if (t & QTDIR)
		*p++ = 'd';
#if 0
	if (t & QTAPPEND)
		*p++ = 'a';
	if (t & QTEXCL)
		*p++ = 'l';
	if (t & QTAUTH)
		*p++ = 'A';
#endif
	*p = '\0';
	return s;
}

int printf_dir(FILE * stream, const struct printf_info *info,
			   const void *const *args)
{
	struct dir *d = *(void **)args[0];
	return fdirconv(stream, d);
}

static int fdirconv(FILE * stream, struct dir *d)
{
	char tmp[16];

	return fprintf(stream, "'%s' '%s' '%s' '%s' "
				   "q " QIDFMT " m %#luo "
				   "at %ld mt %ld l %lld "
				   "t %d d %d",
				   d->name, d->uid, d->gid, d->muid,
				   d->qid.path, d->qid.vers, qidtype(tmp, d->qid.type), d->mode,
				   d->atime, d->mtime, d->length, d->type, d->dev);
}

/*
 * dump out count (or DUMPL, if count is bigger) bytes from
 * buf to stream, as a string if they are all printable,
 * else as a series of hex bytes
 */
#define DUMPL 64

static int dumpsome(FILE * stream, char *buf, long count)
{
	int i, printable, retval = 0;

	if (buf == NULL) {
		return fprintf(stream, "<no data>");
	}
	printable = 1;
	if (count > DUMPL)
		count = DUMPL;
	for (i = 0; i < count && printable; i++)
		if ((buf[i] < 32 && buf[i] != '\n' && buf[i] != '\t')
			|| (uint8_t) buf[i] > 127)
			printable = 0;
	retval += fprintf(stream, "'");
	if (printable) {
		retval += fprintf(stream, "%s", buf);
	} else {
		for (i = 0; i < count; i++) {
			if (i > 0 && i % 4 == 0)
				retval += fprintf(stream, " ");
			retval += fprintf(stream, "%2.2ux", buf[i]);
		}
	}
	retval += fprintf(stream, "'");
	return retval;
}

int printf_fcall_info(const struct printf_info *info, size_t n, int *argtypes,
					  int *size)
{
	if (n > 0) {
		argtypes[0] = PA_POINTER;
		size[0] = sizeof(uint8_t *);
	}
	return 1;
}

int printf_dir_info(const struct printf_info *info, size_t n, int *argtypes,
					int *size)
{
	if (n > 0) {
		argtypes[0] = PA_POINTER;
		size[0] = sizeof(uint8_t *);
	}
	return 1;
}
