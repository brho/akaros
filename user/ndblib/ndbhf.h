/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* a hash file */

struct ndbhf
{
	struct ndbhf	*next;

	int	fd;
	uint32_t	dbmtime;	/* mtime of data base */
	int	hlen;		/* length (in entries) of hash table */
	char	attr[Ndbalen];	/* attribute hashed */

	uint8_t	buf[256];	/* hash file buffer */
	long	off;		/* offset of first byte of buffer */
	int	len;		/* length of valid data in buffer */
};

char*		_ndbparsetuple(char*, struct ndbtuple**);
struct ndbtuple*	_ndbparseline(char*);

#define ISWHITE(x) ((x) == ' ' || (x) == '\t' || (x) == '\r')
#define EATWHITE(x) while(ISWHITE(*(x)))(x)++

extern struct ndbtuple *_ndbtfree;

/* caches */
void	_ndbcacheflush(struct ndb *db);
int	_ndbcachesearch(struct ndb *db, struct ndbs *s, char *attr, char *val,
			   struct ndbtuple **t);
struct ndbtuple* _ndbcacheadd(struct ndb *db, struct ndbs *s, char *attr, char *val,
			      struct ndbtuple *t);
