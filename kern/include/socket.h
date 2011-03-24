#ifndef ROS_SOCKET_H
#define ROS_SOCKET_H

#include <ros/common.h>
#include <sys/queue.h>
#include <atomic.h>
#include <net/pbuf.h>
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
	
	//struct  vnet *so_vnet;      /* network stack instance */
	//struct  protosw *so_proto;  /* (a) protocol handle */
};

struct in_addr {
    uint32_t s_addr;
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



extern struct kmem_cache *sock_kcache;
extern struct kmem_cache *mbuf_kcache;
extern struct kmem_cache *udp_pcb_kcache;

void socket_init();
intreg_t send_iov(struct socket* sock, struct iovec* iov, int flags);
int send_datagram(struct socket* sock, struct iovec* iov, int flags);

intreg_t sys_socket(struct proc *p, int socket_family, int socket_type, int protocol);
intreg_t sys_sendto(struct proc *p, int socket, const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
intreg_t sys_recvfrom(struct proc *p, int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);


#endif /* ROS_SOCKET_H */

