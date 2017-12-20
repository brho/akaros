/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netdb.h>

__BEGIN_DECLS

typedef struct Rock Rock;

enum {
	Ctlsize = 128,

	/* states */
	Sopen = 0,
	Sbound,
	Sconnected,

	/* types of name */
	Tsys = 0,
	Tip,
	Tdom,
};

/*
 *  since BSD programs expect to perform both control and data functions
 *  through a single fd, we need to hide enough info under a rock to
 *  be able to open the control file when we need it.
 */
struct Rock {
	Rock *next;
	unsigned long dev;			/* inode & dev of data file */
	unsigned long inode;		/* ... */
	int domain;					/* from socket call */
	int stype;					/* socket type, from socket()'s type field */
	int sopts;					/* socket options, from socket()'s type field */
	int protocol;				/* ... */
	int reserved;				/* use a priveledged port # (< 1024) */
	union {
		struct sockaddr addr;	/* address from bind */
		struct sockaddr_storage addr_stor;
	};
	union {
		struct sockaddr raddr;	/* peer address */
		struct sockaddr_storage raddr_stor;
	};
	char ctl[Ctlsize];			/* Only used for relative path lookups now */
	int ctl_fd;					/* fd of the ctl file */
	int other;					/* fd of the remote end for Unix domain */
	bool has_listen_fd;			/* has set up a listen file, O_PATH */
	int listen_fd;				/* fd of the listen file, if any */
};

extern Rock *_sock_findrock(int, struct stat *);
extern Rock *_sock_newrock(int);
extern void _sock_fd_closed(int fd);
extern void _sock_srvname(char *, char *);
extern int _sock_srv(char *, int);
extern int _sock_data(int, const char *, int, int, int, Rock **);
extern int _sock_ipattr(const char *);
extern void _sock_get_conv_filename(Rock *r, const char *name, char *retloc);
extern void _sock_ingetaddr(Rock *, struct sockaddr_in *, socklen_t *,
							const char *);
extern int _sock_strip_opts(int type);
extern int _sock_get_opts(int type);
extern void _sock_lookup_rock_fds(int sock_fd, bool can_open_listen_fd,
                                  int *listen_fd_r, int *ctl_fd_r);
extern void _sock_mirror_fcntl(int sock_fd, int cmd, long arg);

int get_sibling_fd(int fd, const char *sibling);
int write_hex_to_fd(int fd, uint64_t num);

/* Integer to string conversion helpers, probably faster than snprintf. */
char *u64_to_str(uint64_t num, char *buf, size_t len);

extern void _syserrno(void);

/* The plan9 UDP header looks like:
 *
 * 52 bytes
 *		raddr (16 b)
 *		laddr (16 b)
 *		IFC addr (ignored if user says it)  (16 b)
 *		rport (2 b) (network ordering)
 *		lport (ignored if user says it) (2b)
 *
 * The v4 addr format is 10 bytes of 0s, then two 0xff, then 4 bytes of addr. */

#define P9_UDP_HDR_SZ 52

/* Takes network-byte ordered IPv4 addr and writes it into buf, in the plan 9 IP
 * addr format */
void naddr_to_plan9addr(uint32_t sin_addr, uint8_t * buf);
/* does v4 only */
uint32_t plan9addr_to_naddr(uint8_t * buf);
/* Returns a rock* if the socket exists and is UDP */
Rock *udp_sock_get_rock(int fd);

__END_DECLS
