/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#ifndef ROS_INC_NDB_H

#define ROS_INC_NDB_H

#include <dir.h>

enum
{
	Ndbalen=	32,	/* max attribute length */
	Ndbvlen=	64,	/* max value length */
};

struct ndbcache;
/*
 *  the database
 */
struct ndb
{
	struct ndb		*next;

	FILE	*b;		/* buffered input file */
	uint8_t		buf[256];	/* and its buffer */

	uint32_t		mtime;		/* mtime of db file */
	struct qid		qid;		/* qid of db file */
	char		file[128];/* path name of db file */
	uint32_t		length;		/* length of db file */
	int		isopen;	/* true if the file is open */

	int		nohash;		/* don't look for hash files */
	struct ndbhf		*hf;		/* open hash files */

	int		ncache;		/* size of tuple cache */
	struct ndbcache	*cache;		/* cached entries */
};

/*
 *  a parsed entry, doubly linked
 */
struct ndbtuple
{
	char		attr[Ndbalen];		/* attribute name */
	char		*val;			/* value(s) */
	struct ndbtuple	*entry;			/* next tuple in this entry */
	struct ndbtuple	*line;			/* next tuple on this line */
	uint32_t		ptr;			/* (for the application - starts 0) */
	char		valbuf[Ndbvlen];	/* initial allocation for value */
};

/*
 *  each hash file is of the form
 *
 *		+---------------------------------------+
 *		|	mtime of db file (4 bytes)	|
 *		+---------------------------------------+
 *		|  size of table (in entries - 4 bytes)	|
 *		+---------------------------------------+
 *		|		hash table		|
 *		+---------------------------------------+
 *		|		hash chains		|
 *		+---------------------------------------+
 *
 *  hash collisions are resolved using chained entries added to the
 *  the end of the hash table.
 *
 *  Hash entries are of the form
 *
 *		+-------------------------------+
 *		|	offset	(3 bytes) 	|
 *		+-------------------------------+
 *
 *  Chain entries are of the form
 *
 *		+-------------------------------+
 *		|	offset1	(3 bytes) 	|
 *		+-------------------------------+
 *		|	offset2	(3 bytes) 	|
 *		+-------------------------------+
 *
 *  The top bit of an offset set to 1 indicates a pointer to a hash chain entry.
 */
#define NDBULLEN	4		/* unsigned long length in bytes */
#define NDBPLEN		3		/* pointer length in bytes */
#define NDBHLEN		(2*NDBULLEN)	/* hash file header length in bytes */

/*
 *  finger pointing to current point in a search
 */
struct ndbs
{
	struct ndb	*db;	/* data base file being searched */
	struct ndbhf	*hf;	/* hash file being searched */
	int	type;
	uint32_t	ptr;	/* current pointer */
	uint32_t	ptr1;	/* next pointer */
	struct ndbtuple *t;	/* last attribute value pair found */
};

struct ndbcache
{
	struct ndbcache	*next;
	char		*attr;
	char		*val;
	struct ndbs		s;
	struct ndbtuple	*t;
};

/*
 *  bit defs for pointers in hash files
 */
#define NDBSPEC 	(1<<23)
#define NDBCHAIN	NDBSPEC		/* points to a collision chain */
#define NDBNAP		(NDBSPEC|1)	/* not a pointer */

/*
 *  macros for packing and unpacking pointers
 */
#define NDBPUTP(v,a) { (a)[0] = v; (a)[1] = (v)>>8; (a)[2] = (v)>>16; }
#define NDBGETP(a) ((a)[0] | ((a)[1]<<8) | ((a)[2]<<16))

/*
 *  macros for packing and unpacking unsigned longs
 */
#define NDBPUTUL(v,a) { (a)[0] = v; (a)[1] = (v)>>8; (a)[2] = (v)>>16; (a)[3] = (v)>>24; }
#define NDBGETUL(a) ((a)[0] | ((a)[1]<<8) | ((a)[2]<<16) | ((a)[3]<<24))

#define NDB_IPlen 16

struct ndbtuple*	csgetval(char*, char*, char*, char*, char*);
char*		csgetvalue(char*, char*, char*, char*, struct ndbtuple**);
struct ndbtuple*	csipinfo(char*, char*, char*, char**, int);
struct ndbtuple*	dnsquery(char*, char*, char*);
char*		ipattr(char*);
struct ndb*		ndbcat(struct ndb*, struct ndb*);
int		ndbchanged(struct ndb*);
void		ndbclose(struct ndb*);
struct ndbtuple*	ndbconcatenate(struct ndbtuple*, struct ndbtuple*);
struct ndbtuple*	ndbdiscard(struct ndbtuple*, struct ndbtuple*);
void		ndbfree(struct ndbtuple*);
struct ndbtuple*	ndbgetipaddr(struct ndb*, char*);
struct ndbtuple*	ndbgetval(struct ndb*,
				  struct ndbs*, char*, char*, char*, char*);
char*		ndbgetvalue(struct ndb*, struct ndbs*, char*, char*, char*,
				 struct ndbtuple**);
struct ndbtuple*	ndbfindattr(struct ndbtuple*, struct ndbtuple*, char*);
uint32_t		ndbhash(char*, int);
struct ndbtuple*	ndbipinfo(struct ndb*, char*, char*, char**, int);
struct ndbtuple*	ndblookval(struct ndbtuple*,
				   struct ndbtuple*, char*, char*);
struct ndbtuple*	ndbnew(char*, char*);
struct ndb*		ndbopen(char*);
struct ndbtuple*	ndbparse(struct ndb*);
int		ndbreopen(struct ndb*);
struct ndbtuple*	ndbreorder(struct ndbtuple*, struct ndbtuple*);
struct ndbtuple*	ndbsearch(struct ndb*, struct ndbs*, char*, char*);
void		ndbsetval(struct ndbtuple*, char*, int);
struct ndbtuple*	ndbsnext(struct ndbs*, char*, char*);
struct ndbtuple*	ndbsubstitute(struct ndbtuple*, struct ndbtuple*,
				      struct ndbtuple*);
char*_ndbparsetuple(char *cp, struct ndbtuple **tp);
struct ndbtuple*_ndbparseline(char *cp);
//void		ndbsetmalloctag(struct ndbtuple*, uintptr_t);
static inline void		ndbsetmalloctag(struct ndbtuple*t, uintptr_t v){}

static inline void werrstr(char *v, ...){}
static inline uintptr_t getcallerpc(void *v){return 0;}
static inline void setmalloctag(void *v){}

void _ndbcacheflush(struct ndb *db);
/* No implementation for this, dumped into a garbage file */
void setnetmtpt(char *net, int n, char *x);

#endif /* ROS_INC_NDB_H */
