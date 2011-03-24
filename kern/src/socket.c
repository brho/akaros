/*
 * Copyright (c) 2011 The Regents of the University of California
 * David Zhu <yuzhu@cs.berkeley.edu>
 * See LICENSE for details.
 * 
 * Socket layer on top of TCP abstraction. Similar to the BSD implementation.
 *
 */
#include <ros/common.h>
#include <socket.h>
#include <vfs.h>
#include <kref.h>
#include <syscall.h>
#include <sys/uio.h>
#include <mbuf.h>
#include <ros/errno.h>
#include <net.h>
#include <net/udp.h>
#include <net/pbuf.h>
#include <umem.h>
/*
 *TODO: Figure out which socket.h is used where
 *There are several socket.h in kern, and a couple more in glibc. Perhaps the glibc ones
 *should grab from here..
 */

struct kmem_cache *sock_kcache;
struct kmem_cache *mbuf_kcache;
struct kmem_cache *udp_pcb_kcache;
// file ops needed to support read/write on socket fd
static struct file_operations socket_op = {
	0,
	0,//soo_read,
	0,//soo_write,
	0,
	0,
	0,
	0,
	0,
	0,
	0,//soo_poll,
	0,
	0,
	0, // sendpage might apply here
	0,
};
static struct socket* getsocket(struct proc *p, int fd){
	/* look up fd -> file */
	struct file *so_file = get_file_from_fd(&(p->open_files), fd);

	/* get socket and verify its type */
	if (so_file == NULL){
		printd("getsocket() fd -> null file: fd %d\n", fd);
		return NULL;
	}
	if (so_file->f_op != &socket_op) {
		set_errno(ENOTSOCK);
		printd("fd %d maps to non-socket file\n");
		return NULL;
	} else
		return (struct socket*) so_file->f_privdata;
}

struct socket* alloc_sock(int socket_family, int socket_type, int protocol){
	struct socket *newsock = kmem_cache_alloc(sock_kcache, 0);
	assert(newsock);

	newsock->so_family = socket_family;
	newsock->so_type = socket_type;
	newsock->so_protocol = protocol;
	newsock->so_state = SS_ISDISCONNECTED;
	pbuf_head_init(&newsock->recv_buff);
	pbuf_head_init(&newsock->send_buff);
	if (socket_type == SOCK_DGRAM){
		newsock->so_pcb = udp_new();
		/* back link */
		((struct udp_pcb*) (newsock->so_pcb))->pcbsock = newsock;
	}
	return newsock;

}
// TODO: refactor vfs so we can allocate fd and do the basic initialization
struct file *alloc_socket_file(struct socket* sock) {
	struct file *file = alloc_file();
	if (file == NULL) return 0;

	// Linux fakes a dentry and an inode for socks, see socket.c : sock_alloc_file
	file->f_dentry = NULL; // This might break things?
	file->f_vfsmnt = 0;
	file->f_flags = 0;

	file->f_mode = S_IRUSR | S_IWUSR; // both read and write for socket files

	file->f_pos = 0;
	file->f_uid = 0;
	file->f_gid = 0;
	file->f_error = 0;

	file->f_op = &socket_op;
	file->f_privdata = sock;
	file->f_mapping = 0;
	return file;
}

void socket_init(){
	
	/* allocate buf for socket */
	sock_kcache = kmem_cache_create("socket", sizeof(struct socket),
									__alignof__(struct socket), 0, 0, 0);
	udp_pcb_kcache = kmem_cache_create("udppcb", sizeof(struct udp_pcb), 
									__alignof__(struct udp_pcb), 0, 0, 0);

	pbuf_init();

}

intreg_t sys_socket(struct proc *p, int socket_family, int socket_type, int protocol){
	//check validity of params
	if (socket_family !=AF_INET && socket_type!=SOCK_DGRAM)
		return 0;
	struct socket *sock = alloc_sock(socket_family, socket_type, protocol);
	struct file *file = alloc_socket_file(sock);
	
	if (file == NULL) return -1;
	int fd = insert_file(&p->open_files, file, 0);
	if (fd < 0) {
		warn("File insertion for socket open failed");
		return -1;
	}
	kref_put(&file->f_kref);
	printk("Socket open, res = %d\n", fd);
	return fd;
}
intreg_t send_iov(struct socket* sock, struct iovec* iov, int flags){
	
	// COPY_COUNT: for each iov, copy into mbuf, and send
	// should not copy here, copy in the protocol..
	// should be esomething like this sock->so_proto->pr_send(sock, iov, flags);
	// make it datagram specific for now...
	send_datagram(sock, iov, flags);
	// finally time to check for validity of UA, in the protocol send
	return 0;	
}
/*TODO: iov support currently broken */
int send_datagram(struct socket* sock, struct iovec* iov, int flags){
	// is this a connection oriented protocol? 
	struct pbuf *prev = NULL;
	struct pbuf *curr = NULL;
	if (sock->so_type == SOCK_STREAM){
		set_errno(ENOTCONN);
		return -1;
	}
	
	// possible sock locks needed
	if ((sock->so_state & SS_ISCONNECTED) == 0){
		set_errno(EINVAL);
		return -1;
	}
    // pbuf_ref needs to map in the user ref
	for (int i = 0; i< sizeof(iov) / sizeof (struct iovec); i++){
		prev = curr;
		curr = pbuf_alloc(PBUF_TRANSPORT, iov[i].iov_len, PBUF_REF);
		if (prev!=NULL) pbuf_chain(prev, curr);
	}
	// struct pbuf* pb = pbuf_alloc(PBUF_TRANSPORT, PBUF_REF);
	udp_send(sock->so_pcb, prev);
	return 0;
	
}

/* sys_sendto can send SOCK_DGRAM and eventually SOCK_STREAM 
 * SOCK_DGRAM uses PBUF_REF since UDP does not need to wait for ack
 * SOCK_STREAM uses PBUF_
 *
 */
intreg_t sys_sendto(struct proc *p_proc, int fd, const void *buffer, size_t length, 
			int flags, const struct sockaddr *dest_addr, socklen_t dest_len){
	// look up the socket
	struct socket* sock = getsocket(p_proc, fd);
	int error;
	struct sockaddr_in *in_addr;
	uint16_t r_port;
	if (sock == NULL) {
		set_errno(EBADF);
		return -1;	
	}
	if (sock->so_type == SOCK_DGRAM){
		in_addr = (struct sockaddr_in *)dest_addr;
		struct pbuf* buf = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_REF);
		if (buf != NULL)
			buf->payload = (void*)buffer;
		else 
			warn("pbuf alloc failed \n");
		// potentially unsafe cast to udp_pcb 
		return udp_sendto((struct udp_pcb*) sock->so_pcb, buf, &in_addr->sin_addr, in_addr->sin_port);
	}

	return -1;
  //TODO: support for sendmsg and iovectors? Let's get the basis working first!
	#if 0 
	// use iovector to handle sendmsg calls too, and potentially scatter-gather
	struct msghdr msg;
	struct iovec iov;
	struct uio auio;
	
	// checking for permission only when you are sending it
	// potential bug TOCTOU, especially with async calls
		
    msg.msg_name = dest_addr;
    msg.msg_namelen = dest_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = 0;
    
	iov.iov_base = buffer;
    iov.iov_len = length;
	

	// this is why we need another function to populate auio

	auio.uio_iov = iov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = 0;
	auio.uio_rw = UIO_WRITE;
	auio.uio_proc = p;

	// consider changing to send_uaio, since we care about progress.
    error = send_iov(soc, iov, flags);
	#endif
}
intreg_t sys_recvfrom(struct proc *p, int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len){
	struct socket* sock = getsocket(p, socket);	
	int copied = 0;
	int returnval = 0;
	if (sock == NULL) {
		set_errno(EBADF);
		return -1;
	}
	if (sock->so_type == SOCK_DGRAM){
		struct pbuf_head *ph = &(sock->recv_buff);
		struct pbuf* buf = NULL;
		/* TODO: busy poll the socket buffer for now */
		while (buf == NULL){
			buf = detach_pbuf(ph);
			if (buf){
				copied = buf->len - sizeof(struct udp_hdr);
				if (copied > length)
					copied = length;
			pbuf_header(buf, -UDP_HDR_SZ);
			printk("loc of payload %p\n", buf->payload);
			// copy it to user space
			returnval = memcpy_to_user_errno(p, buffer, buf->payload, copied);
			}
		}
	}
	if (returnval < 0) 
		return -1;
	else
		return copied;
}
