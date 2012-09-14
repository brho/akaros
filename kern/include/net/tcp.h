#ifndef ROS_KERN_TCP_H
#define ROS_KERN_TCP_H
#include <net/pbuf.h>
#include <net/ip.h>
#include <net.h>
#include <error.h>
#include <bits/netinet.h>
#include <socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TCP_LOCAL_PORT_RANGE_START
#define TCP_LOCAL_PORT_RANGE_START 4096
#define TCP_LOCAL_PORT_RANGE_END   0x7fff
#endif

#ifndef LWIP_CALLBACK_API
#define LWIP_CALLBACK_API 1
#endif 


/**
 * LWIP_TCP_KEEPALIVE==1: Enable TCP_KEEPIDLE, TCP_KEEPINTVL and TCP_KEEPCNT
 * options processing. Note that TCP_KEEPIDLE and TCP_KEEPINTVL have to be set
 * in seconds. (does not require sockets.c, and will affect tcp.c)
 */
#ifndef LWIP_TCP_KEEPALIVE
#define LWIP_TCP_KEEPALIVE              0
#endif

/**
 * TCP_TTL: Default Time-To-Live value.
 */
#ifndef TCP_TTL
#define TCP_TTL                         (255)
#endif

/**
 * TCP_MSS: TCP Maximum segment size. (default is 536, a conservative default,
 * you might want to increase this.)
 * For the receive side, this MSS is advertised to the remote side
 * when opening a connection. For the transmit size, this MSS sets
 * an upper limit on the MSS advertised by the remote host.
 */
#ifndef TCP_MSS
#define TCP_MSS                         (512)
#endif

/**
 * TCP_WND: The size of a TCP window.  This must be at least 
 * (2 * TCP_MSS) for things to work well
 */
#ifndef TCP_WND
#define TCP_WND                         (4 * TCP_MSS)
#endif 

/**
 * TCP_MAXRTX: Maximum number of retransmissions of data segments.
 */
#ifndef TCP_MAXRTX
#define TCP_MAXRTX                      12
#endif

/**
 * TCP_SYNMAXRTX: Maximum number of retransmissions of SYN segments.
 */
#ifndef TCP_SYNMAXRTX
#define TCP_SYNMAXRTX                   6
#endif

/**
 * TCP_QUEUE_OOSEQ==1: TCP will queue segments that arrive out of order.
 * Define to 0 if your device is low on memory.
 */
#ifndef TCP_QUEUE_OOSEQ
#define TCP_QUEUE_OOSEQ                 0
#endif


/**
 * TCP_CALCULATE_EFF_SEND_MSS: "The maximum size of a segment that TCP really
 * sends, the 'effective send MSS,' MUST be the smaller of the send MSS (which
 * reflects the available reassembly buffer size at the remote host) and the
 * largest size permitted by the IP layer" (RFC 1122)
 * Setting this to 1 enables code that checks TCP_MSS against the MTU of the
 * netif used for a connection and limits the MSS if it would be too big otherwise.
 */
#ifndef TCP_CALCULATE_EFF_SEND_MSS
#define TCP_CALCULATE_EFF_SEND_MSS      1
#endif


/**
 * TCP_SND_BUF: TCP sender buffer space (bytes). 
 */
#ifndef TCP_SND_BUF
#define TCP_SND_BUF                     256
#endif

/**
 * TCP_SND_QUEUELEN: TCP sender buffer space (pbufs). This must be at least
 * as much as (2 * TCP_SND_BUF/TCP_MSS) for things to work.
 */
#ifndef TCP_SND_QUEUELEN
#define TCP_SND_QUEUELEN                ((4 * (TCP_SND_BUF) + (TCP_MSS - 1))/(TCP_MSS))
#endif

/**
 * TCP_SNDLOWAT: TCP writable space (bytes). This must be less than
 * TCP_SND_BUF. It is the amount of space which must be available in the
 * TCP snd_buf for select to return writable (combined with TCP_SNDQUEUELOWAT).
 */
#ifndef TCP_SNDLOWAT
#define TCP_SNDLOWAT                    ((TCP_SND_BUF)/2)
#endif

/**
 * TCP_SNDQUEUELOWAT: TCP writable bufs (pbuf count). This must be grater
 * than TCP_SND_QUEUELEN. If the number of pbufs queued on a pcb drops below
 * this number, select returns writable (combined with TCP_SNDLOWAT).
 */
#ifndef TCP_SNDQUEUELOWAT
#define TCP_SNDQUEUELOWAT               ((TCP_SND_QUEUELEN)/2)
#endif

/**
 * TCP_LISTEN_BACKLOG: Enable the backlog option for tcp listen pcb.
 */
#ifndef TCP_LISTEN_BACKLOG
#define TCP_LISTEN_BACKLOG              0
#endif

/**
 * The maximum allowed backlog for TCP listen netconns.
 * This backlog is used unless another is explicitly specified.
 * 0xff is the maximum (u8_t).
 */
#ifndef TCP_DEFAULT_LISTEN_BACKLOG
#define TCP_DEFAULT_LISTEN_BACKLOG      0xff
#endif

/**
 * TCP_OVERSIZE: The maximum number of bytes that tcp_write may
 * allocate ahead of time in an attempt to create shorter pbuf chains
 * for transmission. The meaningful range is 0 to TCP_MSS. Some
 * suggested values are:
 *
 * 0:         Disable oversized allocation. Each tcp_write() allocates a new
              pbuf (old behaviour).
 * 1:         Allocate size-aligned pbufs with minimal excess. Use this if your
 *            scatter-gather DMA requires aligned fragments.
 * 128:       Limit the pbuf/memory overhead to 20%.
 * TCP_MSS:   Try to create unfragmented TCP packets.
 * TCP_MSS/4: Try to create 4 fragments or less per TCP packet.
 */
#ifndef TCP_OVERSIZE
#define TCP_OVERSIZE                    TCP_MSS
#endif

/**
 * LWIP_TCP_TIMESTAMPS==1: support the TCP timestamp option.
 */
#ifndef LWIP_TCP_TIMESTAMPS
#define LWIP_TCP_TIMESTAMPS             0
#endif

/**
 * TCP_WND_UPDATE_THRESHOLD: difference in window to trigger an
 * explicit window update
 */
#ifndef TCP_WND_UPDATE_THRESHOLD
#define TCP_WND_UPDATE_THRESHOLD   (TCP_WND / 4)
#endif

struct tcp_pcb;

#define TCP_PRIO_MIN    1
#define TCP_PRIO_NORMAL 64
#define TCP_PRIO_MAX    127

/** Function prototype for tcp accept callback functions. Called when a new
 * connection can be accepted on a listening pcb.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param newpcb The new connection pcb
 * @param err An error code if there has been an error accepting.
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef error_t (*tcp_accept_fn)(void *arg, struct tcp_pcb *newpcb, error_t err);

/** Function prototype for tcp receive callback functions. Called when data has
 * been received.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb which received data
 * @param p The received data (or NULL when the connection has been closed!)
 * @param err An error code if there has been an error receiving
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef error_t (*tcp_recv_fn)(void *arg, struct tcp_pcb *tpcb,
                             struct pbuf *p, error_t err);

/** Function prototype for tcp sent callback functions. Called when sent data has
 * been acknowledged by the remote side. Use it to free corresponding resources.
 * This also means that the pcb has now space available to send new data.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb for which data has been acknowledged
 * @param len The amount of bytes acknowledged
 * @return ERR_OK: try to send some data by calling tcp_output
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef error_t (*tcp_sent_fn)(void *arg, struct tcp_pcb *tpcb,
                              uint16_t len);

/** Function prototype for tcp poll callback functions. Called periodically as
 * specified by @see tcp_poll.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb tcp pcb
 * @return ERR_OK: try to send some data by calling tcp_output
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef error_t (*tcp_poll_fn)(void *arg, struct tcp_pcb *tpcb);

/** Function prototype for tcp error callback functions. Called when the pcb
 * receives a RST or is unexpectedly closed for any other reason.
 *
 * @note The corresponding pcb is already freed when this callback is called!
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param err Error code to indicate why the pcb has been closed
 *            ERR_ABRT: aborted through tcp_abort or by a TCP timer
 *            ERR_RST: the connection was reset by the remote host
 */
typedef void  (*tcp_err_fn)(void *arg, error_t err);

/** Function prototype for tcp connected callback functions. Called when a pcb
 * is connected to the remote side after initiating a connection attempt by
 * calling tcp_connect().
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb which is connected
 * @param err An unused error code, always ERR_OK currently ;-) TODO!
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 *
 * @note When a connection attempt fails, the error callback is currently called!
 */
typedef error_t (*tcp_connected_fn)(void *arg, struct tcp_pcb *tpcb, error_t err);

enum tcp_state {
  CLOSED      = 0,
  LISTEN      = 1,
  SYN_SENT    = 2,
  SYN_RCVD    = 3,
  ESTABLISHED = 4,
  FIN_WAIT_1  = 5,
  FIN_WAIT_2  = 6,
  CLOSE_WAIT  = 7,
  CLOSING     = 8,
  LAST_ACK    = 9,
  TIME_WAIT   = 10
};

/**
 * members common to struct tcp_pcb and struct tcp_listen_pcb
 */
#define TCP_PCB_COMMON(type) \
  type *next; /* for the linked list */ \
  enum tcp_state state; /* TCP state */ \
  uint8_t prio; \
  void *callback_arg; \
	tcp_accept_fn accept; \
  /* ports are in host byte order */ \
  uint16_t local_port


/* the TCP protocol control block */
struct tcp_pcb {
	IP_PCB;

/** protocol specific PCB members */
  TCP_PCB_COMMON(struct tcp_pcb);
	struct socket *pcbsock;

  /* ports are in host byte order */
  uint16_t remote_port;
  
  uint8_t flags;
#define TF_ACK_DELAY   ((uint8_t)0x01U)   /* Delayed ACK. */
#define TF_ACK_NOW     ((uint8_t)0x02U)   /* Immediate ACK. */
#define TF_INFR        ((uint8_t)0x04U)   /* In fast recovery. */
#define TF_TIMESTAMP   ((uint8_t)0x08U)   /* Timestamp option enabled */
#define TF_RXCLOSED    ((uint8_t)0x10U)   /* rx closed by tcp_shutdown */
#define TF_FIN         ((uint8_t)0x20U)   /* Connection was closed locally (FIN segment enqueued). */
#define TF_NODELAY     ((uint8_t)0x40U)   /* Disable Nagle algorithm */
#define TF_NAGLEMEMERR ((uint8_t)0x80U)   /* nagle enabled, memerr, try to output to prevent delayed ACK to happen */

  /* the rest of the fields are in host byte order
     as we have to do some math with them */
  /* receiver variables */
  uint32_t rcv_nxt;   /* next seqno expected */
  uint16_t rcv_wnd;   /* receiver window available */
  uint16_t rcv_ann_wnd; /* receiver window to announce */
  uint32_t rcv_ann_right_edge; /* announced right edge of window */

  /* Timers */
  uint32_t tmr;
  uint8_t polltmr, pollinterval;
  
  /* Retransmission timer. */
  int16_t rtime;
  
  uint16_t mss;   /* maximum segment size */
  
  /* RTT (round trip time) estimation variables */
  uint32_t rttest; /* RTT estimate in 500ms ticks */
  uint32_t rtseq;  /* sequence number being timed */
  int16_t sa, sv; /* @todo document this */

  int16_t rto;    /* retransmission time-out */
  uint8_t nrtx;    /* number of retransmissions */

  /* fast retransmit/recovery */
  uint32_t lastack; /* Highest acknowledged seqno. */
  uint8_t dupacks;
  
  /* congestion avoidance/control variables */
  uint16_t cwnd;  
  uint16_t ssthresh;

  /* sender variables */
  uint32_t snd_nxt;   /* next new seqno to be sent */
  uint16_t snd_wnd;   /* sender window */
  uint32_t snd_wl1, snd_wl2; /* Sequence and acknowledgement numbers of last
                             window update. */
  uint32_t snd_lbb;       /* Sequence number of next byte to be buffered. */

  uint16_t acked;
  
  uint16_t snd_buf;   /* Available buffer space for sending (in bytes). */
#define TCP_SNDQUEUELEN_OVERFLOW (0xffff-3)
  uint16_t snd_queuelen; /* Available buffer space for sending (in tcp_segs). */

#if TCP_OVERSIZE
  /* Extra bytes available at the end of the last pbuf in unsent. */
  uint16_t unsent_oversize;
#endif /* TCP_OVERSIZE */ 

  /* These are ordered by sequence number: */
  struct tcp_seg *unsent;   /* Unsent (queued) segments. */
  struct tcp_seg *unacked;  /* Sent but unacknowledged segments. */
#if TCP_QUEUE_OOSEQ  
  struct tcp_seg *ooseq;    /* Received out of sequence segments. */
#endif /* TCP_QUEUE_OOSEQ */

  struct pbuf *refused_data; /* Data previously received but not yet taken by upper layer */

  /* Function to be called when more send buffer space is available. */
  tcp_sent_fn sent;
  /* Function to be called when (in-sequence) data has arrived. */
  tcp_recv_fn recv;
  /* Function to be called when a connection has been set up. */
  tcp_connected_fn connected;
  /* Function which is called periodically. */
  tcp_poll_fn poll;
  /* Function to be called whenever a fatal error occurs. */
  tcp_err_fn errf;

#if LWIP_TCP_TIMESTAMPS
  uint32_t ts_lastacksent;
  uint32_t ts_recent;
#endif /* LWIP_TCP_TIMESTAMPS */

  /* idle time before KEEPALIVE is sent */
  uint32_t keep_idle;
#if LWIP_TCP_KEEPALIVE
  uint32_t keep_intvl;
  uint32_t keep_cnt;
#endif /* LWIP_TCP_KEEPALIVE */
  
  /* Persist timer counter */
  uint32_t persist_cnt;
  /* Persist timer back-off */
  uint8_t persist_backoff;

  /* KEEPALIVE counter */
  uint8_t keep_cnt_sent;
};

struct tcp_pcb_listen {  
	IP_PCB;
/* Protocol specific PCB members */
  TCP_PCB_COMMON(struct tcp_pcb_listen);

#if TCP_LISTEN_BACKLOG
  uint8_t backlog;
  uint8_t accepts_pending;
#endif /* TCP_LISTEN_BACKLOG */
};

#if 0 //LWIP_EVENT_API

enum lwip_event {
  LWIP_EVENT_ACCEPT,
  LWIP_EVENT_SENT,
  LWIP_EVENT_RECV,
  LWIP_EVENT_CONNECTED,
  LWIP_EVENT_POLL,
  LWIP_EVENT_ERR
};

error_t lwip_tcp_event(void *arg, struct tcp_pcb *pcb,
         enum lwip_event,
         struct pbuf *p,
         uint16_t size,
         error_t err);

#endif /* LWIP_EVENT_API */

/* Application program's interface: */
struct tcp_pcb * tcp_new (void);
error_t tcp_bind(struct tcp_pcb *pcb, const struct in_addr *ipaddr, uint16_t port);
void             tcp_abort (struct tcp_pcb *pcb);

void             tcp_arg     (struct tcp_pcb *pcb, void *arg);
void             tcp_accept  (struct tcp_pcb *pcb, tcp_accept_fn accept);
void             tcp_recv    (struct tcp_pcb *pcb, tcp_recv_fn recv);
void             tcp_sent    (struct tcp_pcb *pcb, tcp_sent_fn sent);
void             tcp_poll    (struct tcp_pcb *pcb, tcp_poll_fn poll, uint8_t interval);
void             tcp_err     (struct tcp_pcb *pcb, tcp_err_fn err);

#define          tcp_mss(pcb)             (((pcb)->flags & TF_TIMESTAMP) ? ((pcb)->mss - 12)  : (pcb)->mss)
#define          tcp_sndbuf(pcb)          ((pcb)->snd_buf)
#define          tcp_sndqueuelen(pcb)     ((pcb)->snd_queuelen)
#define          tcp_nagle_disable(pcb)   ((pcb)->flags |= TF_NODELAY)
#define          tcp_nagle_enable(pcb)    ((pcb)->flags &= ~TF_NODELAY)
#define          tcp_nagle_disabled(pcb)  (((pcb)->flags & TF_NODELAY) != 0)

#if TCP_LISTEN_BACKLOG
#define          tcp_accepted(pcb) do { \
  LWIP_ASSERT("pcb->state == LISTEN (called for wrong pcb?)", pcb->state == LISTEN); \
  (((struct tcp_pcb_listen *)(pcb))->accepts_pending--); } while(0)
#else  /* TCP_LISTEN_BACKLOG */
#define          tcp_accepted(pcb) LWIP_ASSERT("pcb->state == LISTEN (called for wrong pcb?)", \
                                               pcb->state == LISTEN)
#endif /* TCP_LISTEN_BACKLOG */

void             tcp_recved  (struct tcp_pcb *pcb, uint16_t len);
error_t            tcp_connect (struct tcp_pcb *pcb, ip_addr_t *ipaddr,
                              uint16_t port, tcp_connected_fn connected);

struct tcp_pcb * tcp_listen_with_backlog(struct tcp_pcb *pcb, uint8_t backlog);
#define          tcp_listen(pcb) tcp_listen_with_backlog(pcb, TCP_DEFAULT_LISTEN_BACKLOG)

error_t            tcp_close   (struct tcp_pcb *pcb);
error_t            tcp_shutdown(struct tcp_pcb *pcb, int shut_rx, int shut_tx);

/* Flags for "apiflags" parameter in tcp_write */
#define TCP_WRITE_FLAG_COPY 0x01
#define TCP_WRITE_FLAG_MORE 0x02

error_t            tcp_write   (struct tcp_pcb *pcb, const void *dataptr, uint16_t len,
                              uint8_t apiflags);

void             tcp_setprio (struct tcp_pcb *pcb, uint8_t prio);

#define TCP_PRIO_MIN    1
#define TCP_PRIO_NORMAL 64
#define TCP_PRIO_MAX    127

error_t            tcp_output  (struct tcp_pcb *pcb);


const char* tcp_debug_state_str(enum tcp_state s);

#ifdef __cplusplus
}
#endif

#endif


