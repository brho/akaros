/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2017 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#pragma once

#include <net/ip.h>

enum {
	QMAX = 64 * 1024 - 1,
	IP_TCPPROTO = 6,

	TCP4_IPLEN = 8,
	TCP4_PHDRSIZE = 12,
	TCP4_HDRSIZE = 20,
	TCP4_TCBPHDRSZ = 40,
	TCP4_PKT = TCP4_IPLEN + TCP4_PHDRSIZE,

	TCP6_IPLEN = 0,
	TCP6_PHDRSIZE = 40,
	TCP6_HDRSIZE = 20,
	TCP6_TCBPHDRSZ = 60,
	TCP6_PKT = TCP6_IPLEN + TCP6_PHDRSIZE,

	TcptimerOFF = 0,
	TcptimerON = 1,
	TcptimerDONE = 2,
	MAX_TIME = (1 << 20),	/* Forever */
	TCP_ACK = 50,	/* Timed ack sequence in ms */
	MAXBACKMS = 9 * 60 * 1000, /* longest backoff time (ms) before hangup */

	URG = 0x20,	/* Data marked urgent */
	ACK = 0x10,	/* Acknowledge is valid */
	PSH = 0x08,	/* Whole data pipe is pushed */
	RST = 0x04,	/* Reset connection */
	SYN = 0x02,	/* Pkt. is synchronise */
	FIN = 0x01,	/* Start close down */

	EOLOPT = 0,
	NOOPOPT = 1,
	MSSOPT = 2,
	MSS_LENGTH = 4,	/* max segment size header option length */
	WSOPT = 3,
	WS_LENGTH = 3,	/* WS header option length */
	MAX_WS_VALUE = 14, /* RFC specified.  Limits available window to 2^30 */
	TS_OPT = 8,
	TS_LENGTH = 10,
	TS_SEND_PREPAD = 2, /* For non-SYNs, pre-pad 2 nops for 32 byte align */
	SACK_OK_OPT = 4,
	SACK_OK_LENGTH = 2,
	SACK_OPT = 5,
	MSL2 = 10,
	MSPTICK = 50,	/* Milliseconds per timer tick */
	DEF_MSS = 1460,	/* Default mean segment */
	DEF_MSS6 = 1280,	/* Default mean segment (min) for v6 */
	SACK_SUPPORTED = TRUE,	/* SACK is on by default */
	MAX_NR_SACKS_PER_PACKET = 4,	/* limited by TCP's opts size */
	MAX_NR_SND_SACKS = 10,
	MAX_NR_RCV_SACKS = 3,	/* We could try for 4, but don't need to */
	DEF_RTT = 500,	/* Default round trip */
	DEF_KAT = 120000,	/* Default time (ms) between keep alives */
	TCP_LISTEN = 0,	/* Listen connection */
	TCP_CONNECT = 1,	/* Outgoing connection */
	SYNACK_RXTIMER = 250,	/* ms between SYNACK retransmits */

	TCPREXMTTHRESH = 3,	/* dupack threshold for recovery */
	SACK_RETRANS_RECOVERY = 1,
	FAST_RETRANS_RECOVERY = 2,
	RTO_RETRANS_RECOVERY = 3,
	CWIND_SCALE = 10,	/* initial CWIND will be MSS * this */

	FORCE			= 1 << 0,
	CLONE			= 1 << 1,
	ACTIVE			= 1 << 2,
	SYNACK			= 1 << 3,
	TSO				= 1 << 4,

	RTTM_ALPHA_SHIFT = 3,	/* alpha = 1/8 */
	RTTM_BRAVO_SHIFT = 2,	/* bravo = 1/4 (beta) */

	Closed = 0,	/* Connection states */
	Listen,
	Syn_sent,
	Established,
	Finwait1,
	Finwait2,
	Close_wait,
	Closing,
	Last_ack,
	Time_wait,

	Maxlimbo = 1000,/* maximum procs waiting for response to SYN ACK */
	NLHT = 256,	/* hash table size, must be a power of 2 */
	LHTMASK = NLHT - 1,

	HaveWS = 1 << 8,
};

typedef struct tcptimer Tcptimer;
struct tcptimer {
	Tcptimer *next;
	Tcptimer *prev;
	Tcptimer *readynext;
	int state;
	uint64_t start;
	uint64_t count;
	void (*func) (void *);
	void *arg;
};

struct tcphdr {
	uint8_t tcpsport[2];
	uint8_t tcpdport[2];
	uint8_t tcpseq[4];
	uint8_t tcpack[4];
	uint8_t tcpflag[2];
	uint8_t tcpwin[2];
	uint8_t tcpcksum[2];
	uint8_t tcpurg[2];
	/* Options segment */
	uint8_t tcpopt[1];
};

/* v4 and v6 pseudo headers used for checksumming tcp
 *
 * Note the field layout is the same for a real IP packet.  "Unused" in v4 is
 * the TTL slot, but it's the 'zeros' for the TCP PH csum.  Similarly, tcplen is
 * the IP csum slot.  Later on, it'll get overwritten in the IP stack or in
 * hardware.  The struct tcp4hdr (or rather bp->rp) will be cast to an Ip4hdr.
 */
typedef struct tcp4hdr Tcp4hdr;
struct tcp4hdr {
	uint8_t vihl;			/* Version and header length */
	uint8_t tos;			/* Type of service */
	uint8_t length[2];		/* packet length */
	uint8_t id[2];			/* Identification */
	uint8_t frag[2];		/* Fragment information */
	uint8_t Unused;
	uint8_t proto;
	uint8_t tcplen[2];
	uint8_t tcpsrc[4];
	uint8_t tcpdst[4];
	struct tcphdr;
};

typedef struct tcp6hdr Tcp6hdr;
struct tcp6hdr {
	uint8_t vcf[4];
	uint8_t ploadlen[2];
	uint8_t proto;
	uint8_t ttl;
	uint8_t tcpsrc[IPaddrlen];
	uint8_t tcpdst[IPaddrlen];
	struct tcphdr;
};

struct sack_block {
	uint32_t left;
	uint32_t right;
};

/*
 *  this represents the control info
 *  for a single packet.  It is derived from
 *  a packet in ntohtcp{4,6}() and stuck into
 *  a packet in htontcp{4,6}().
 */
typedef struct tcp Tcp;
struct tcp {
	uint16_t source;
	uint16_t dest;
	uint32_t seq;
	uint32_t ack;
	uint8_t flags;
	uint16_t ws;		/* window scale option (if not zero) */
	uint32_t wnd;
	uint16_t urg;
	uint16_t mss;		/* max segment size option (if not zero) */
	uint16_t len;		/* size of data */
	uint32_t ts_val;	/* timestamp val from sender */
	uint32_t ts_ecr;	/* timestamp echo response from sender */
	bool sack_ok;		/* header had/should have SACK_PERMITTED */
	uint8_t nr_sacks;
	struct sack_block sacks[MAX_NR_SACKS_PER_PACKET];
};

/*
 *  this header is malloc'd to thread together fragments
 *  waiting to be coalesced
 */
typedef struct reseq Reseq;
struct reseq {
	Reseq *next;
	Tcp seg;
	struct block *bp;
	uint16_t length;
};

/*
 *  the qlock in the Conv locks this structure
 */
typedef struct tcpctl Tcpctl;
struct tcpctl {
	uint8_t state;		/* Connection state */
	uint8_t type;		/* Listening or active connection */
	uint8_t code;		/* Icmp code */
	struct {
		uint32_t una;	/* Left edge of unacked data region */
		uint32_t nxt;	/* Next seq to send, right edge of unacked */
		uint32_t rtx;	/* Next to send for retrans */
		uint32_t wnd;	/* Tcp send window */
		uint32_t urg;	/* Urgent data pointer */
		uint32_t wl2;
		int scale;	/* how much to right shift window for xmit */
		uint32_t in_flight;	/* estimate of how much is in flight */
		uint8_t loss_hint;	/* number of loss hints rcvd */
		uint8_t sack_loss_hint;	/* For detecting sack rxmit losses */
		bool flush_sacks;	/* Two timeouts in a row == dump sacks */
		uint8_t recovery;	/* loss recovery flag */
		uint32_t recovery_pt;	/* right window for recovery point */
		uint8_t nr_sacks;
		struct sack_block sacks[MAX_NR_SND_SACKS];
	} snd;
	struct {
		uint32_t nxt;	/* Receive pointer to next uint8_t slot */
		uint32_t wnd;	/* Receive window incoming */
		uint32_t urg;	/* Urgent pointer */
		int blocked;
		int una;	/* unacked data segs */
		int scale;	/* how much to left shift window for rx */
		uint8_t nr_sacks;
		struct sack_block sacks[MAX_NR_RCV_SACKS];
	} rcv;
	uint32_t iss;		/* Initial sequence number */
	int sawwsopt;		/* true if we saw a wsopt on the incoming SYN */
	uint32_t cwind;		/* Congestion window */
	int scale;		/* desired snd.scale */
	uint32_t ssthresh;	/* Slow start threshold */
	int irs;		/* Initial received squence */
	uint16_t mss;		/* Max segment size */
	uint16_t typical_mss;	/* MSS for most packets (< MSS for some opts) */
	int rerecv;		/* Overlap of data rerecevived */
	uint32_t window;	/* Recevive window */
	uint8_t backoff;	/* Exponential backoff counter */
	int backedoff;		/* ms we've backed off for rexmits */
	uint8_t flags;		/* State flags */
	Reseq *reseq;		/* Resequencing queue */
	Tcptimer timer;		/* Activity timer */
	Tcptimer acktimer;	/* Acknowledge timer */
	Tcptimer rtt_timer;	/* Round trip timer */
	Tcptimer katimer;	/* keep alive timer */
	uint32_t rttseq;	/* Round trip sequence */
	int srtt;		/* Shortened round trip */
	int mdev;		/* Mean deviation of round trip */
	int kacounter;		/* count down for keep alive */
	uint64_t sndsyntime;	/* time syn sent */
	uint64_t time;		/* time Finwait2 was sent */
	int nochecksum;		/* non-zero means don't send checksums */
	int flgcnt;		/* number of flags in the sequence (FIN,SYN) */
	uint32_t ts_recent;	/* timestamp received around last_ack_sent */
	uint32_t last_ack_sent;	/* to determine when to update timestamp */
	bool sack_ok;		/* Can use SACK for this connection */
	struct Ipifc *ifc;	/* Uncounted ref */

	union {
		Tcp4hdr tcp4hdr;
		Tcp6hdr tcp6hdr;
	} protohdr;		/* prototype header */
};

/* New calls are put in limbo rather than having a conversation structure
 *  allocated.  Thus, a SYN attack results in lots of limbo'd calls but not any
 *  real Conv structures mucking things up.  Calls in limbo rexmit their SYN ACK
 *  every SYNACK_RXTIMER ms up to 4 times, i.e., they disappear after 1 second.
 *
 *  In particular they aren't on a listener's queue so that they don't figure in
 *  the input queue limit.
 *
 *  If 1/2 of a T3 was attacking SYN packets, we'ld have a permanent queue of
 *  70000 limbo'd calls.  Not great for a linear list but doable.  Therefore
 *  there is no hashing of this list.
 */
typedef struct limbo Limbo;
struct limbo {
	Limbo *next;

	uint8_t laddr[IPaddrlen];
	uint8_t raddr[IPaddrlen];
	uint16_t lport;
	uint16_t rport;
	uint32_t irs;			/* initial received sequence */
	uint32_t iss;			/* initial sent sequence */
	uint16_t mss;			/* mss from the other end */
	uint16_t rcvscale;		/* how much to scale rcvd windows */
	uint16_t sndscale;		/* how much to scale sent windows */
	uint64_t lastsend;		/* last time we sent a synack */
	uint8_t version;		/* v4 or v6 */
	uint8_t rexmits;		/* number of retransmissions */
	bool sack_ok;			/* other side said SACK_OK */
	uint32_t ts_val;		/* timestamp val from sender */
	struct Ipifc *ifc;		/* Uncounted ref */
};

enum {
	/* MIB stats */
	MaxConn,
	ActiveOpens,
	PassiveOpens,
	EstabResets,
	CurrEstab,
	InSegs,
	OutSegs,
	RetransSegs,
	RetransTimeouts,
	InErrs,
	OutRsts,

	/* non-MIB stats */
	CsumErrs,
	HlenErrs,
	LenErrs,
	OutOfOrder,

	Nstats
};

typedef struct tcppriv Tcppriv;
struct tcppriv {
	/* List of active timers */
	qlock_t tl;
	Tcptimer *timers;

	/* hash table for matching conversations */
	struct Ipht ht;

	/* calls in limbo waiting for an ACK to our SYN ACK */
	int nlimbo;
	Limbo *lht[NLHT];

	/* for keeping track of tcpackproc */
	qlock_t apl;
	int ackprocstarted;

	uint32_t stats[Nstats];
};

static inline int seq_within(uint32_t x, uint32_t low, uint32_t high)
{
	if (low <= high) {
		if (low <= x && x <= high)
			return 1;
	} else {
		if (x >= low || x <= high)
			return 1;
	}
	return 0;
}

static inline int seq_lt(uint32_t x, uint32_t y)
{
	return (int)(x - y) < 0;
}

static inline int seq_le(uint32_t x, uint32_t y)
{
	return (int)(x - y) <= 0;
}

static inline int seq_gt(uint32_t x, uint32_t y)
{
	return (int)(x - y) > 0;
}

static inline int seq_ge(uint32_t x, uint32_t y)
{
	return (int)(x - y) >= 0;
}

static inline uint32_t seq_max(uint32_t x, uint32_t y)
{
	return seq_ge(x, y) ? x : y;
}

static inline uint32_t seq_min(uint32_t x, uint32_t y)
{
	return seq_le(x, y) ? x : y;
}

/* Caller needs to know we're TCP and with transport_offset set, which is
 * usually on the outbound network path. */
static inline struct tcphdr *tcp_hdr(struct block *bp)
{
	return (struct tcphdr*)(bp->rp + bp->transport_offset);
}

static inline size_t tcp_hdrlen(struct block *bp)
{
	struct tcphdr *hdr = tcp_hdr(bp);
	uint8_t data_offset;

	data_offset = hdr->tcpflag[0] >> 4;
	return data_offset * 4;
}
