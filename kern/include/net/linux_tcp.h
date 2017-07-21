/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP module.
 *
 * Version:	@(#)tcp.h	1.0.5	05/23/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/* From include/uapi/linux/tcp.h */

enum tcp_ca_state {
	TCP_CA_Open = 0,
#define TCPF_CA_Open	(1<<TCP_CA_Open)
	TCP_CA_Disorder = 1,
#define TCPF_CA_Disorder (1<<TCP_CA_Disorder)
	TCP_CA_CWR = 2,
#define TCPF_CA_CWR	(1<<TCP_CA_CWR)
	TCP_CA_Recovery = 3,
#define TCPF_CA_Recovery (1<<TCP_CA_Recovery)
	TCP_CA_Loss = 4
#define TCPF_CA_Loss	(1<<TCP_CA_Loss)
};

/* From include/net/tcp.h */

#define	TCP_ECN_OK		1
#define	TCP_ECN_QUEUE_CWR	2
#define	TCP_ECN_DEMAND_CWR	4
#define	TCP_ECN_SEEN		8

enum tcp_tw_status {
	TCP_TW_SUCCESS = 0,
	TCP_TW_RST = 1,
	TCP_TW_ACK = 2,
	TCP_TW_SYN = 3
};


/* TCP uses 32bit jiffies to save some space.
 * Note that this is different from tcp_time_stamp, which
 * historically has been the same until linux-4.13.
 */
#define tcp_jiffies32 ((u32)jiffies)

/* Events passed to congestion control interface */
enum tcp_ca_event {
	CA_EVENT_TX_START,	/* first transmit when no packets in flight */
	CA_EVENT_CWND_RESTART,	/* congestion window restart */
	CA_EVENT_COMPLETE_CWR,	/* end of congestion recovery */
	CA_EVENT_LOSS,		/* loss timeout */
	CA_EVENT_ECN_NO_CE,	/* ECT set, but not CE marked */
	CA_EVENT_ECN_IS_CE,	/* received CE marked IP packet */
	CA_EVENT_DELAYED_ACK,	/* Delayed ack is sent */
	CA_EVENT_NON_DELAYED_ACK,
};

/* Information about inbound ACK, passed to cong_ops->in_ack_event() */
enum tcp_ca_ack_event_flags {
	CA_ACK_SLOWPATH		= (1 << 0),	/* In slow path processing */
	CA_ACK_WIN_UPDATE	= (1 << 1),	/* ACK updated window */
	CA_ACK_ECE		= (1 << 2),	/* ECE bit is set on ack */
};

/*
 * Interface for adding new TCP congestion control handlers
 */
#define TCP_CA_NAME_MAX	16
#define TCP_CA_MAX	128
#define TCP_CA_BUF_MAX	(TCP_CA_NAME_MAX*TCP_CA_MAX)

#define TCP_CA_UNSPEC	0

/* Algorithm can be set on socket without CAP_NET_ADMIN privileges */
#define TCP_CONG_NON_RESTRICTED 0x1
/* Requires ECN/ECT set on all packets */
#define TCP_CONG_NEEDS_ECN	0x2

union tcp_cc_info;

struct ack_sample {
	u32 pkts_acked;
	s32 rtt_us;
	u32 in_flight;
};

/* A rate sample measures the number of (original/retransmitted) data
 * packets delivered "delivered" over an interval of time "interval_us".
 * The tcp_rate.c code fills in the rate sample, and congestion
 * control modules that define a cong_control function to run at the end
 * of ACK processing can optionally chose to consult this sample when
 * setting cwnd and pacing rate.
 * A sample is invalid if "delivered" or "interval_us" is negative.
 */
struct rate_sample {
	u64  prior_mstamp; /* starting timestamp for interval */
	u32  prior_delivered;	/* tp->delivered at "prior_mstamp" */
	s32  delivered;		/* number of packets delivered over interval */
	long interval_us;	/* time for tp->delivered to incr "delivered" */
	long rtt_us;		/* RTT of last (S)ACKed packet (or -1) */
	int  losses;		/* number of packets marked lost upon ACK */
	u32  acked_sacked;	/* number of packets newly (S)ACKed upon ACK */
	u32  prior_in_flight;	/* in flight before this ACK */
	bool is_app_limited;	/* is sample from packet with bubble in pipe? */
	bool is_retrans;	/* is sample from retransmission? */
};

struct tcp_congestion_ops {
	struct list_head	list;
	u32 key;
	u32 flags;

	/* initialize private data (optional) */
	void (*init)(struct sock *sk);
	/* cleanup private data  (optional) */
	void (*release)(struct sock *sk);

	/* return slow start threshold (required) */
	u32 (*ssthresh)(struct sock *sk);
	/* do new cwnd calculation (required) */
	void (*cong_avoid)(struct sock *sk, u32 ack, u32 acked);
	/* call before changing ca_state (optional) */
	void (*set_state)(struct sock *sk, u8 new_state);
	/* call when cwnd event occurs (optional) */
	void (*cwnd_event)(struct sock *sk, enum tcp_ca_event ev);
	/* call when ack arrives (optional) */
	void (*in_ack_event)(struct sock *sk, u32 flags);
	/* new value of cwnd after loss (required) */
	u32  (*undo_cwnd)(struct sock *sk);
	/* hook for packet ack accounting (optional) */
	void (*pkts_acked)(struct sock *sk, const struct ack_sample *sample);
	/* suggest number of segments for each skb to transmit (optional) */
	u32 (*tso_segs_goal)(struct sock *sk);
	/* returns the multiplier used in tcp_sndbuf_expand (optional) */
	u32 (*sndbuf_expand)(struct sock *sk);
	/* call when packets are delivered to update cwnd and pacing rate,
	 * after all the ca_state processing. (optional)
	 */
	void (*cong_control)(struct sock *sk, const struct rate_sample *rs);
	/* get info for inet_diag (optional) */
	size_t (*get_info)(struct sock *sk, u32 ext, int *attr,
			   union tcp_cc_info *info);

	char 		name[TCP_CA_NAME_MAX];
	struct module 	*owner;
};
