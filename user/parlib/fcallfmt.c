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
#include <nixip.h>
#include <dir.h>
#include <ndb.h>
#include <fcall.h>

static void dumpsome(FILE*, char*, long);
static void fdirconv(FILE*, struct dir*);
static char *qidtype(char*, uint8_t);

#define	QIDFMT	"(%.16llux %lud %s)"

int printf_fcall(FILE *stream, const struct printf_info *info,
                   const void *const *args)
{
	struct fcall *f = *(void**)args[0];
	int fid, type, tag, i;
	char buf[512], tmp[200];
	char *p, *e;
	struct dir *d;
	struct qid *q;

	e = buf+sizeof(buf);
	type = f->type;
	fid = f->fid;
	tag = f->tag;
	switch(type){
	case Tversion:	/* 100 */
		fprintf(stream,"Tversion tag %ud msize %ud version '%s'", tag, f->msize, f->version);
		break;
	case Rversion:
		fprintf(stream,"Rversion tag %ud msize %ud version '%s'", tag, f->msize, f->version);
		break;
	case Tauth:	/* 102 */
		fprintf(stream,"Tauth tag %ud afid %d uname %s aname %s", tag,
			f->afid, f->uname, f->aname);
		break;
	case Rauth:
		fprintf(stream,"Rauth tag %ud qid " QIDFMT, tag,
			f->aqid.path, f->aqid.vers, qidtype(tmp, f->aqid.type));
		break;
	case Tattach:	/* 104 */
		fprintf(stream,"Tattach tag %ud fid %d afid %d uname %s aname %s", tag,
			fid, f->afid, f->uname, f->aname);
		break;
	case Rattach:
		fprintf(stream,"Rattach tag %ud qid " QIDFMT, tag,
			f->qid.path, f->qid.vers, qidtype(tmp, f->qid.type));
		break;
	case Rerror:	/* 107; 106 (Terror) illegal */
		fprintf(stream,"Rerror tag %ud ename %s", tag, f->ename);
		break;
	case Tflush:	/* 108 */
		fprintf(stream,"Tflush tag %ud oldtag %ud", tag, f->oldtag);
		break;
	case Rflush:
		fprintf(stream,"Rflush tag %ud", tag);
		break;
	case Twalk:	/* 110 */
		fprintf(stream,"Twalk tag %ud fid %d newfid %d nwname %d ", tag, fid, f->newfid, f->nwname);
		if(f->nwname <= MAXWELEM)
			for(i=0; i<f->nwname; i++)
				fprintf(stream, "%d:%s ", i, f->wname[i]);
		break;
	case Rwalk:
		fprintf(stream,"Rwalk tag %ud nwqid %ud ", tag, f->nwqid);
		if(f->nwqid <= MAXWELEM)
			for(i=0; i<f->nwqid; i++){
				q = &f->wqid[i];
				fprintf(stream, "%d:" QIDFMT " ", i,
					q->path, q->vers, qidtype(tmp, q->type));
			}
		break;
	case Topen:	/* 112 */
		fprintf(stream,"Topen tag %ud fid %ud mode %d", tag, fid, f->mode);
		break;
	case Ropen:
		fprintf(stream,"Ropen tag %ud qid " QIDFMT " iounit %ud ", tag,
			f->qid.path, f->qid.vers, qidtype(tmp, f->qid.type), f->iounit);
		break;
	case Tcreate:	/* 114 */
		fprintf(stream,"Tcreate tag %ud fid %ud name %s perm %M mode %d", tag, fid, f->name, (uint32_t)f->perm, f->mode);
		break;
	case Rcreate:
		fprintf(stream,"Rcreate tag %ud qid " QIDFMT " iounit %ud ", tag,
			f->qid.path, f->qid.vers, qidtype(tmp, f->qid.type), f->iounit);
		break;
	case Tread:	/* 116 */
		fprintf(stream,"Tread tag %ud fid %d offset %lld count %ud",
			tag, fid, f->offset, f->count);
		break;
	case Rread:
		fprintf(stream,"Rread tag %ud count %ud ", tag, f->count);
			dumpsome(stream, f->data, f->count);
		break;
	case Twrite:	/* 118 */
		fprintf(stream,"Twrite tag %ud fid %d offset %lld count %ud ",
			tag, fid, f->offset, f->count);
		dumpsome(stream, f->data, f->count);
		break;
	case Rwrite:
		fprintf(stream,"Rwrite tag %ud count %ud", tag, f->count);
		break;
	case Tclunk:	/* 120 */
		fprintf(stream,"Tclunk tag %ud fid %ud", tag, fid);
		break;
	case Rclunk:
		fprintf(stream,"Rclunk tag %ud", tag);
		break;
	case Tremove:	/* 122 */
		fprintf(stream,"Tremove tag %ud fid %ud", tag, fid);
		break;
	case Rremove:
		fprintf(stream,"Rremove tag %ud", tag);
		break;
	case Tstat:	/* 124 */
		fprintf(stream,"Tstat tag %ud fid %ud", tag, fid);
		break;
	case Rstat:
		fprintf(stream,"Rstat tag %ud ", tag);
		if(f->nstat > sizeof tmp)
			fprintf(stream, " stat(%d bytes)", f->nstat);
		else{
			d = (struct dir*)tmp;
			convM2D(f->stat, f->nstat, d, (char*)(d+1));
			fprintf(stream, " stat ");
			fdirconv(stream, d);
		}
		break;
	case Twstat:	/* 126 */
		fprintf(stream,"Twstat tag %ud fid %ud", tag, fid);
		if(f->nstat > sizeof tmp)
			fprintf(stream, " stat(%d bytes)", f->nstat);
		else{
			d = (struct dir*)tmp;
			convM2D(f->stat, f->nstat, d, (char*)(d+1));
			fprintf(stream, " stat ");
			fdirconv(stream, d);
		}
		break;
	case Rwstat:
		fprintf(stream,"Rwstat tag %ud", tag);
		break;
	default:
		fprintf(stream, "unknown type %d", type);
	}
	return 0;
}

static char*
qidtype(char *s, uint8_t t)
{
	char *p;
#define QTDIR              0x80	/* type bit for directories */
	p = s;
	if(t & QTDIR)
		*p++ = 'd';
#if 0
	if(t & QTAPPEND)
		*p++ = 'a';
	if(t & QTEXCL)
		*p++ = 'l';
	if(t & QTAUTH)
		*p++ = 'A';
#endif
	*p = '\0';
	return s;
}

#if 0
int
dirfmt(Fmt *fmt)
{
	char buf[160];

	fdirconv(buf, buf+sizeof buf, va_arg(fmt->args, struct dir*));
	return fmtstrcpy(fmt, buf);
}
#endif

static void
	fdirconv(FILE *stream, struct dir *d)
{
	char tmp[16];

	fprintf(stream,"'%s' '%s' '%s' '%s' "
		"q " QIDFMT " m %#luo "
		"at %ld mt %ld l %lld "
		"t %d d %d",
			d->name, d->uid, d->gid, d->muid,
			d->qid.path, d->qid.vers, qidtype(tmp, d->qid.type), d->mode,
			d->atime, d->mtime, d->length,
			d->type, d->dev);
}

/*
 * dump out count (or DUMPL, if count is bigger) bytes from
 * buf to ans, as a string if they are all printable,
 * else as a series of hex bytes
 */
#define DUMPL 64

static void
	dumpsome(FILE *stream, char *buf, long count)
{
#if 0
later
	int i, printable;
	char *p;

	if(buf == NULL){
	fprintf(stream, "<no data>");

	}
	printable = 1;
	if(count > DUMPL)
		count = DUMPL;
	for(i=0; i<count && printable; i++)
		if((buf[i]<32 && buf[i] !='\n' && buf[i] !='\t') || (uint8_t)buf[i]>127)
			printable = 0;
	fprintf(stream,"'");
	if(printable){
		if(count > e-p-2)
			count = e-p-2;
		//fprintf(stream, "%s", 
	}else{
		if(2*count > e-p-2)
			count = (e-p-2)/2;
		for(i=0; i<count; i++){
			if(i>0 && i%4==0)
				*p++ = ' ';
			fprintf(stream, "%2.2ux", buf[i]);
			p += 2;
		}
	}
	*p++ = '\'';
	*p = 0;
	return p - ans;
#endif
}

int printf_fcall_info(const struct printf_info* info, size_t n, int *argtypes,
                        int *size)
{
	if (n > 0) {
		argtypes[0] = PA_POINTER;
		size[0] = sizeof(uint8_t*);
	}
	return 1;
}
