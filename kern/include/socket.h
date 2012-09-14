#ifndef ROS_SOCKET_H
#define ROS_SOCKET_H

#include <ros/common.h>
#include <sys/queue.h>
#include <atomic.h>
#include <net/pbuf.h>
#include <kthread.h>
#include <net/ip.h>
#include <vfs.h>
// Just a couple of AF types that we might support
#define AF_UNSPEC	0
#define AF_UNIX		1	/* Unix domain sockets 		*/
#define AF_LOCAL	1	/* POSIX name for AF_UNIX	*/
#define AF_INET		2	/* Internet IP Protocol 	*/

#define PF_UNSPEC	AF_UNSPEC
#define PF_UNIX		AF_UNIX
#define PF_LOCAL	AF_LOCAL
#define PF_INET		AF_INET

#define	SS_NOFDREF		0x0001	/* no file table ref any more */
#define	SS_ISCONNECTED		0x0002	/* socket connected to a peer */
#define	SS_ISCONNECTING		0x0004	/* in process of connecting to peer */
#define	SS_ISDISCONNECTING	0x0008	/* in process of disconnecting */
#define	SS_NBIO			0x0100	/* non-blocking ops */
#define	SS_ASYNC		0x0200	/* async i/o notify */
#define	SS_ISCONFIRMING		0x0400	/* deciding to accept connection req */
#define	SS_ISDISCONNECTED	0x2000	/* socket disconnected from peer */

/* Define an range for automatic port assignment */
#define SOCKET_PORT_START 4096
#define SOCKET_PORT_END  0x7fff

struct socket;
struct proc;
// These are probably defined elsewhere too..
#ifndef socklen_t
typedef int socklen_t;
typedef int sa_family_t;
#endif
#define inet_addr_to_ipaddr_p(target_ipaddr_p, source_inaddr)   ((target_ipaddr_p) = (ip_addr_t*)&((source_inaddr)->s_addr))
enum sock_type {
    SOCK_STREAM = 1,
    SOCK_DGRAM  = 2,
    SOCK_RAW    = 3,
    SOCK_RDM    = 4,
    SOCK_SEQPACKET  = 5,
    SOCK_DCCP   = 6,
    SOCK_PACKET = 10,
};

struct socket{
  //int so_count;       /* (b) reference count */
  short   so_type;        /* (a) generic type, see socket.h */
	short 	so_family;
	int	so_protocol;
  short   so_options;     /* from socket call, see socket.h */
  //short   so_linger;      /* time to linger while closing */
  short   so_state;       /* (b) internal state flags SS_* */
	//int so_qstate;      /* (e) internal state flags SQ_* */
	void    *so_pcb;        /* protocol control block */
	struct pbuf_head recv_buff;
	struct pbuf_head send_buff;
	struct semaphore sem;
	spinlock_t waiter_lock;
	struct semaphore_list waiters;   /* semaphone to for a process to sleep on */
	
	//struct  vnet *so_vnet;      /* network stack instance */
	//struct  protosw *so_proto;  /* (a) protocol handle */
};


/* members are in network byte order */
struct sockaddr_in {
    // uint8_t sin_len; -- bsd only field
    uint8_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};


struct sockaddr {
	unsigned char	sa_len;		/* bsd only total length */
	sa_family_t	sa_family;	/* address family */
	char		sa_data[14];	/* actually longer; address value */
};

/*
 * Message header for recvmsg and sendmsg calls.
 * Used value-result for recvmsg, value only for sendmsg.
 */ 
struct msghdr {
    void        *msg_name;      /* optional address */
    socklen_t    msg_namelen;       /* size of address */
    struct iovec    *msg_iov;       /* scatter/gather array */
    int      msg_iovlen;        /* # elements in msg_iov */
    void        *msg_control;       /* ancillary data, see below */
    socklen_t    msg_controllen;    /* ancillary data buffer len */
    int      msg_flags;     /* flags on received message */
};


/* Socket-level options for `getsockopt' and `setsockopt'.  */
enum
  {
    SO_DEBUG = 0x0001,		/* Record debugging information.  */
#define SO_DEBUG SO_DEBUG
    SO_ACCEPTCONN = 0x0002,	/* Accept connections on socket.  */
#define SO_ACCEPTCONN SO_ACCEPTCONN
    SO_REUSEADDR = 0x0004,	/* Allow reuse of local addresses.  */
#define SO_REUSEADDR SO_REUSEADDR
    SO_KEEPALIVE = 0x0008,	/* Keep connections alive and send
				   SIGPIPE when they die.  */
#define SO_KEEPALIVE SO_KEEPALIVE
    SO_DONTROUTE = 0x0010,	/* Don't do local routing.  */
#define SO_DONTROUTE SO_DONTROUTE
    SO_BROADCAST = 0x0020,	/* Allow transmission of
				   broadcast messages.  */
#define SO_BROADCAST SO_BROADCAST
    SO_USELOOPBACK = 0x0040,	/* Use the software loopback to avoid
				   hardware use when possible.  */
#define SO_USELOOPBACK SO_USELOOPBACK
    SO_LINGER = 0x0080,		/* Block on close of a reliable
				   socket to transmit pending data.  */
#define SO_LINGER SO_LINGER
    SO_OOBINLINE = 0x0100,	/* Receive out-of-band data in-band.  */
#define SO_OOBINLINE SO_OOBINLINE
    SO_REUSEPORT = 0x0200,	/* Allow local address and port reuse.  */
#define SO_REUSEPORT SO_REUSEPORT
    SO_SNDBUF = 0x1001,		/* Send buffer size.  */
#define SO_SNDBUF SO_SNDBUF
    SO_RCVBUF = 0x1002,		/* Receive buffer.  */
#define SO_RCVBUF SO_RCVBUF
    SO_SNDLOWAT = 0x1003,	/* Send low-water mark.  */
#define SO_SNDLOWAT SO_SNDLOWAT
    SO_RCVLOWAT = 0x1004,	/* Receive low-water mark.  */
#define SO_RCVLOWAT SO_RCVLOWAT
    SO_SNDTIMEO = 0x1005,	/* Send timeout.  */
#define SO_SNDTIMEO SO_SNDTIMEO
    SO_RCVTIMEO = 0x1006,	/* Receive timeout.  */
#define SO_RCVTIMEO SO_RCVTIMEO
    SO_ERROR = 0x1007,		/* Get and clear error status.  */
#define SO_ERROR SO_ERROR
    SO_STYLE = 0x1008,		/* Get socket connection style.  */
#define SO_STYLE SO_STYLE
    SO_TYPE = SO_STYLE		/* Compatible name for SO_STYLE.  */
#define SO_TYPE SO_TYPE
  };
#define SO_INHERITED   (SO_REUSEADDR|SO_KEEPALIVE|SO_LINGER)

extern struct kmem_cache *sock_kcache;
extern struct kmem_cache *mbuf_kcache;
extern struct kmem_cache *udp_pcb_kcache;
extern struct kmem_cache *tcp_pcb_kcache;
extern struct kmem_cache *tcp_pcb_listen_kcache;
extern struct kmem_cache *tcp_segment_kcache;


void socket_init();
intreg_t send_iov(struct socket* sock, struct iovec* iov, int flags);
int send_datagram(struct socket* sock, struct iovec* iov, int flags);

intreg_t sys_socket(struct proc *p, int socket_family, int socket_type, int protocol);
intreg_t sys_sendto(struct proc *p, int socket, const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
intreg_t sys_recvfrom(struct proc *p, int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);
intreg_t sys_select(struct proc *p, int nfds, fd_set *readfds, fd_set *writefds,
				fd_set *exceptfds, struct timeval *timeout);
intreg_t sys_connect(struct proc *p, int sockfd, const struct sockaddr *addr, socklen_t addrlen);
intreg_t sys_send(struct proc *p, int sockfd, const void *buf, size_t len, int flags);
intreg_t sys_recv(struct proc *p, int sockfd, void *buf, size_t len, int flags);
intreg_t sys_bind(struct proc* p, int sockfd, const struct sockaddr *addr, socklen_t addrlen);
intreg_t sys_accept(struct proc *p, int sockfd, struct sockaddr *addr, socklen_t *addrlen);
intreg_t sys_listen(struct proc *p, int sockfd, int backlog);


#endif /* ROS_SOCKET_H */

