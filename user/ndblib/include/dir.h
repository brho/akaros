#ifndef ROS_INC_DIR_H
#define ROS_INC_DIR_H

/* STATFIXLEN includes leading 16-bit count */
/* The count, however, excludes itself; total size is BIT16SZ+count */
#define STATFIXLEN	(BIT16SZ+QIDSZ+5*BIT16SZ+4*BIT32SZ+1*BIT64SZ)	/* amount of fixed length data in a stat buffer */

struct qid
{
	uint64_t	path;
	uint32_t	vers;
	uint8_t	type;
};

struct dir {
	/* system-modified data */
	uint16_t	type;	/* server type */
	unsigned int	dev;	/* server subtype */
	/* file data */
	struct qid	qid;	/* unique id from server */
	uint32_t	mode;	/* permissions */
	uint32_t	atime;	/* last read time */
	uint32_t	mtime;	/* last write time */
	int64_t	length;	/* file length: see <u.h> */
	char	*name;	/* last element of path */
	char	*uid;	/* owner name */
	char	*gid;	/* group name */
	char	*muid;	/* last modifier name */
};

#endif /* ROS_INC_DIR_H */
