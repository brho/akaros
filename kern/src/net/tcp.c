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

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <net/ip.h>
#include <net/tcp.h>

/* Must correspond to the enumeration in tcp.h */
static char *tcpstates[] = {
	"Closed", "Listen", "Syn_sent",
	"Established", "Finwait1", "Finwait2", "Close_wait",
	"Closing", "Last_ack", "Time_wait"
};

static int tcp_irtt = DEF_RTT;			/* Initial guess at round trip time */
static uint16_t tcp_mss = DEF_MSS;		/* Maximum segment size to be sent */

/* Must correspond to the enumeration in tcp.h */
static char *statnames[] = {
	[MaxConn] "MaxConn",
	[ActiveOpens] "ActiveOpens",
	[PassiveOpens] "PassiveOpens",
	[EstabResets] "EstabResets",
	[CurrEstab] "CurrEstab",
	[InSegs] "InSegs",
	[OutSegs] "OutSegs",
	[RetransSegs] "RetransSegs",
	[RetransTimeouts] "RetransTimeouts",
	[InErrs] "InErrs",
	[OutRsts] "OutRsts",
	[CsumErrs] "CsumErrs",
	[HlenErrs] "HlenErrs",
	[LenErrs] "LenErrs",
	[OutOfOrder] "OutOfOrder",
};

/*
 *  Setting tcpporthogdefense to non-zero enables Dong Lin's
 *  solution to hijacked systems staking out port's as a form
 *  of DoS attack.
 *
 *  To avoid stateless Conv hogs, we pick a sequence number at random.  If
 *  it that number gets acked by the other end, we shut down the connection.
 *  Look for tcpporthogedefense in the code.
 */
static int tcpporthogdefense = 0;

static int addreseq(Tcpctl *, struct tcppriv *, Tcp *, struct block *,
                    uint16_t);
static void getreseq(Tcpctl *, Tcp *, struct block **, uint16_t *);
static void localclose(struct conv *, char *unused_char_p_t);
static void procsyn(struct conv *, Tcp *);
static void tcpiput(struct Proto *, struct Ipifc *, struct block *);
static void tcpoutput(struct conv *);
static int tcptrim(Tcpctl *, Tcp *, struct block **, uint16_t *);
static void tcpstart(struct conv *, int);
static void tcptimeout(void *);
static void tcpsndsyn(struct conv *, Tcpctl *);
static void tcprcvwin(struct conv *);
static void tcpacktimer(void *);
static void tcpkeepalive(void *);
static void tcpsetkacounter(Tcpctl *);
static void tcprxmit(struct conv *);
static void tcpsettimer(Tcpctl *);
static void tcpsynackrtt(struct conv *);
static void tcpsetscale(struct conv *, Tcpctl *, uint16_t, uint16_t);
static void tcp_loss_event(struct conv *s, Tcpctl *tcb);
static uint16_t derive_payload_mss(Tcpctl *tcb);
static void set_in_flight(Tcpctl *tcb);

static void limborexmit(struct Proto *);
static void limbo(struct conv *, uint8_t *unused_uint8_p_t, uint8_t *, Tcp *,
				  int);

static void tcpsetstate(struct conv *s, uint8_t newstate)
{
	Tcpctl *tcb;
	uint8_t oldstate;
	struct tcppriv *tpriv;

	tpriv = s->p->priv;

	tcb = (Tcpctl *) s->ptcl;

	oldstate = tcb->state;
	if (oldstate == newstate)
		return;

	if (oldstate == Established)
		tpriv->stats[CurrEstab]--;
	if (newstate == Established)
		tpriv->stats[CurrEstab]++;

	/**
	print( "%d/%d %s->%s CurrEstab=%d\n", s->lport, s->rport,
		tcpstates[oldstate], tcpstates[newstate], tpriv->tstats.tcpCurrEstab );
	**/

	switch (newstate) {
		case Closed:
			qclose(s->rq);
			qclose(s->wq);
			qclose(s->eq);
			break;

		case Close_wait:	/* Remote closes */
			qhangup(s->rq, NULL);
			break;
	}

	tcb->state = newstate;

	if (oldstate == Syn_sent && newstate != Closed)
		Fsconnected(s, NULL);
}

static void tcpconnect(struct conv *c, char **argv, int argc)
{
	Fsstdconnect(c, argv, argc);
	tcpstart(c, TCP_CONNECT);
}

static int tcpstate(struct conv *c, char *state, int n)
{
	Tcpctl *s;

	s = (Tcpctl *) (c->ptcl);

	return snprintf(state, n,
					"%s qin %d qout %d srtt %d mdev %d cwin %u swin %u>>%d rwin %u>>%d timer.start %llu timer.count %llu rerecv %d katimer.start %d katimer.count %d\n",
					tcpstates[s->state],
					c->rq ? qlen(c->rq) : 0,
					c->wq ? qlen(c->wq) : 0,
					s->srtt, s->mdev,
					s->cwind, s->snd.wnd, s->rcv.scale, s->rcv.wnd,
					s->snd.scale, s->timer.start, s->timer.count, s->rerecv,
					s->katimer.start, s->katimer.count);
}

static int tcpinuse(struct conv *c)
{
	Tcpctl *s;

	s = (Tcpctl *) (c->ptcl);
	return s->state != Closed;
}

static void tcpannounce(struct conv *c, char **argv, int argc)
{
	Fsstdannounce(c, argv, argc);
	tcpstart(c, TCP_LISTEN);
	Fsconnected(c, NULL);
}

static void tcpbypass(struct conv *cv, char **argv, int argc)
{
	struct tcppriv *tpriv = cv->p->priv;

	Fsstdbypass(cv, argv, argc);
	iphtadd(&tpriv->ht, cv);
}

static void tcpshutdown(struct conv *c, int how)
{
	Tcpctl *tcb = (Tcpctl*)c->ptcl;

	/* Do nothing for the read side */
	if (how == SHUT_RD)
		return;
	/* Sends a FIN.  If we're in another state (like Listen), we'll run into
	 * issues, since we'll never send the FIN.  We'll be shutdown on our end,
	 * but we'll never tell the distant end.  Might just be an app issue. */
	switch (tcb->state) {
	case Established:
		tcb->flgcnt++;
		tcpsetstate(c, Finwait1);
		tcpoutput(c);
		break;
	}
}

/*
 *  tcpclose is always called with the q locked
 */
static void tcpclose(struct conv *c)
{
	Tcpctl *tcb;

	tcb = (Tcpctl *) c->ptcl;

	qhangup(c->rq, NULL);
	qhangup(c->wq, NULL);
	qhangup(c->eq, NULL);
	qflush(c->rq);

	switch (tcb->state) {
		case Listen:
			/*
			 *  reset any incoming calls to this listener
			 */
			Fsconnected(c, "Hangup");

			localclose(c, NULL);
			break;
		case Closed:
		case Syn_sent:
			localclose(c, NULL);
			break;
		case Established:
			tcb->flgcnt++;
			tcpsetstate(c, Finwait1);
			tcpoutput(c);
			break;
		case Close_wait:
			tcb->flgcnt++;
			tcpsetstate(c, Last_ack);
			tcpoutput(c);
			break;
	}
}

static void tcpkick(void *x)
{
	ERRSTACK(1);
	struct conv *s = x;
	Tcpctl *tcb;

	tcb = (Tcpctl *) s->ptcl;

	qlock(&s->qlock);
	if (waserror()) {
		qunlock(&s->qlock);
		nexterror();
	}

	switch (tcb->state) {
		case Syn_sent:
		case Established:
		case Close_wait:
			/*
			 * Push data
			 */
			tcprcvwin(s);
			tcpoutput(s);
			break;
		default:
			localclose(s, "Hangup");
			break;
	}

	qunlock(&s->qlock);
	poperror();
}

static void tcprcvwin(struct conv *s)
{
	/* Call with tcb locked */
	int w;
	Tcpctl *tcb;

	tcb = (Tcpctl *) s->ptcl;
	w = tcb->window - qlen(s->rq);
	if (w < 0)
		w = 0;

	/* RFC 813: Avoid SWS.  We'll always reduce the window (because the qio
	 * increased - that's legit), and we'll always advertise the window
	 * increases (corresponding to qio drains) when those are greater than MSS.
	 * But we don't advertise increases less than MSS.
	 *
	 * Note we don't shrink the window at all - that'll result in tcptrim()
	 * dropping packets that were sent before the sender gets our update. */
	if ((w < tcb->rcv.wnd) || (w >= tcb->mss))
		tcb->rcv.wnd = w;
	/* We've delayed sending an update to rcv.wnd, and we might never get
	 * another ACK to drive the TCP stack after the qio is drained.  We could
	 * replace this stuff with qio kicks or callbacks, but that might be
	 * trickier with the MSS limitation.  (and 'edge' isn't empty or not). */
	if (w < tcb->mss)
		tcb->rcv.blocked = 1;
}

static void tcpacktimer(void *v)
{
	ERRSTACK(1);
	Tcpctl *tcb;
	struct conv *s;

	s = v;
	tcb = (Tcpctl *) s->ptcl;

	qlock(&s->qlock);
	if (waserror()) {
		qunlock(&s->qlock);
		nexterror();
	}
	if (tcb->state != Closed) {
		tcb->flags |= FORCE;
		tcprcvwin(s);
		tcpoutput(s);
	}
	qunlock(&s->qlock);
	poperror();
}

static void tcpcreate(struct conv *c)
{
	/* We don't use qio limits.  Instead, TCP manages flow control on its own.
	 * We only use qpassnolim().  Note for qio that 0 doesn't mean no limit. */
	c->rq = qopen(0, Qcoalesce, 0, 0);
	c->wq = qopen(8 * QMAX, Qkick, tcpkick, c);
}

static void timerstate(struct tcppriv *priv, Tcptimer *t, int newstate)
{
	if (newstate != TcptimerON) {
		if (t->state == TcptimerON) {
			// unchain
			if (priv->timers == t) {
				priv->timers = t->next;
				if (t->prev != NULL)
					panic("timerstate1");
			}
			if (t->next)
				t->next->prev = t->prev;
			if (t->prev)
				t->prev->next = t->next;
			t->next = t->prev = NULL;
		}
	} else {
		if (t->state != TcptimerON) {
			// chain
			if (t->prev != NULL || t->next != NULL)
				panic("timerstate2");
			t->prev = NULL;
			t->next = priv->timers;
			if (t->next)
				t->next->prev = t;
			priv->timers = t;
		}
	}
	t->state = newstate;
}

static void tcpackproc(void *a)
{
	ERRSTACK(1);
	Tcptimer *t, *tp, *timeo;
	struct Proto *tcp;
	struct tcppriv *priv;
	int loop;

	tcp = a;
	priv = tcp->priv;

	for (;;) {
		kthread_usleep(MSPTICK * 1000);

		qlock(&priv->tl);
		timeo = NULL;
		loop = 0;
		for (t = priv->timers; t != NULL; t = tp) {
			if (loop++ > 10000)
				panic("tcpackproc1");
			tp = t->next;
			if (t->state == TcptimerON) {
				t->count--;
				if (t->count == 0) {
					timerstate(priv, t, TcptimerDONE);
					t->readynext = timeo;
					timeo = t;
				}
			}
		}
		qunlock(&priv->tl);

		loop = 0;
		for (t = timeo; t != NULL; t = t->readynext) {
			if (loop++ > 10000)
				panic("tcpackproc2");
			if (t->state == TcptimerDONE && t->func != NULL) {
				/* discard error style */
				if (!waserror())
					(*t->func) (t->arg);
				poperror();
			}
		}

		limborexmit(tcp);
	}
}

static void tcpgo(struct tcppriv *priv, Tcptimer *t)
{
	if (t == NULL || t->start == 0)
		return;

	qlock(&priv->tl);
	t->count = t->start;
	timerstate(priv, t, TcptimerON);
	qunlock(&priv->tl);
}

static void tcphalt(struct tcppriv *priv, Tcptimer *t)
{
	if (t == NULL)
		return;

	qlock(&priv->tl);
	timerstate(priv, t, TcptimerOFF);
	qunlock(&priv->tl);
}

static int backoff(int n)
{
	return 1 << n;
}

static void localclose(struct conv *s, char *reason)
{
	/* called with tcb locked */
	Tcpctl *tcb;
	Reseq *rp, *rp1;
	struct tcppriv *tpriv;

	tpriv = s->p->priv;
	tcb = (Tcpctl *) s->ptcl;

	iphtrem(&tpriv->ht, s);

	tcphalt(tpriv, &tcb->timer);
	tcphalt(tpriv, &tcb->rtt_timer);
	tcphalt(tpriv, &tcb->acktimer);
	tcphalt(tpriv, &tcb->katimer);

	/* Flush reassembly queue; nothing more can arrive */
	for (rp = tcb->reseq; rp != NULL; rp = rp1) {
		rp1 = rp->next;
		freeblist(rp->bp);
		kfree(rp);
	}
	tcb->reseq = NULL;

	if (tcb->state == Syn_sent)
		Fsconnected(s, reason);

	qhangup(s->rq, reason);
	qhangup(s->wq, reason);

	tcpsetstate(s, Closed);

	/* listener will check the rq state */
	if (s->state == Announced)
		rendez_wakeup(&s->listenr);
}

/* mtu (- TCP + IP hdr len) of 1st hop */
static int tcpmtu(struct Ipifc *ifc, int version, int *scale)
{
	int mtu;

	switch (version) {
		default:
		case V4:
			mtu = DEF_MSS;
			if (ifc != NULL)
				mtu = ifc->maxtu - ifc->m->hsize - (TCP4_PKT + TCP4_HDRSIZE);
			break;
		case V6:
			mtu = DEF_MSS6;
			if (ifc != NULL)
				mtu = ifc->maxtu - ifc->m->hsize - (TCP6_PKT + TCP6_HDRSIZE);
			break;
	}
	*scale = HaveWS | 7;

	return mtu;
}

static void tcb_check_tso(Tcpctl *tcb)
{
	/* This can happen if the netdev isn't up yet. */
	if (!tcb->ifc)
		return;
	if (tcb->ifc->feat & NETF_TSO)
		tcb->flags |= TSO;
	else
		tcb->flags &= ~TSO;
}

static void inittcpctl(struct conv *s, int mode)
{
	Tcpctl *tcb;
	Tcp4hdr *h4;
	Tcp6hdr *h6;
	int mss;

	tcb = (Tcpctl *) s->ptcl;

	memset(tcb, 0, sizeof(Tcpctl));

	tcb->ssthresh = UINT32_MAX;
	tcb->srtt = tcp_irtt;
	tcb->mdev = 0;

	/* setup timers */
	tcb->timer.start = tcp_irtt / MSPTICK;
	tcb->timer.func = tcptimeout;
	tcb->timer.arg = s;
	tcb->rtt_timer.start = MAX_TIME;
	tcb->acktimer.start = TCP_ACK / MSPTICK;
	tcb->acktimer.func = tcpacktimer;
	tcb->acktimer.arg = s;
	tcb->katimer.start = DEF_KAT / MSPTICK;
	tcb->katimer.func = tcpkeepalive;
	tcb->katimer.arg = s;

	mss = DEF_MSS;

	/* create a prototype(pseudo) header */
	if (mode != TCP_LISTEN) {
		if (ipcmp(s->laddr, IPnoaddr) == 0)
			findlocalip(s->p->f, s->laddr, s->raddr);

		switch (s->ipversion) {
			case V4:
				h4 = &tcb->protohdr.tcp4hdr;
				memset(h4, 0, sizeof(*h4));
				h4->proto = IP_TCPPROTO;
				hnputs(h4->tcpsport, s->lport);
				hnputs(h4->tcpdport, s->rport);
				v6tov4(h4->tcpsrc, s->laddr);
				v6tov4(h4->tcpdst, s->raddr);
				break;
			case V6:
				h6 = &tcb->protohdr.tcp6hdr;
				memset(h6, 0, sizeof(*h6));
				h6->proto = IP_TCPPROTO;
				hnputs(h6->tcpsport, s->lport);
				hnputs(h6->tcpdport, s->rport);
				ipmove(h6->tcpsrc, s->laddr);
				ipmove(h6->tcpdst, s->raddr);
				mss = DEF_MSS6;
				break;
			default:
				panic("inittcpctl: version %d", s->ipversion);
		}
	}

	tcb->ifc = findipifc(s->p->f, s->laddr, 0);
	tcb->mss = mss;
	tcb->typical_mss = mss;
	tcb->cwind = tcb->typical_mss * CWIND_SCALE;

	/* default is no window scaling */
	tcb->window = QMAX;
	tcb->rcv.wnd = QMAX;
	tcb->rcv.scale = 0;
	tcb->snd.scale = 0;
	tcb_check_tso(tcb);
}

/*
 *  called with s qlocked
 */
static void tcpstart(struct conv *s, int mode)
{
	Tcpctl *tcb;
	struct tcppriv *tpriv;
	char *kpname;

	tpriv = s->p->priv;

	if (tpriv->ackprocstarted == 0) {
		qlock(&tpriv->apl);
		if (tpriv->ackprocstarted == 0) {
			/* tcpackproc needs to free this if it ever exits */
			kpname = kmalloc(KNAMELEN, MEM_WAIT);
			snprintf(kpname, KNAMELEN, "#I%dtcpack", s->p->f->dev);
			ktask(kpname, tcpackproc, s->p);
			tpriv->ackprocstarted = 1;
		}
		qunlock(&tpriv->apl);
	}

	tcb = (Tcpctl *) s->ptcl;

	inittcpctl(s, mode);

	iphtadd(&tpriv->ht, s);
	switch (mode) {
		case TCP_LISTEN:
			tpriv->stats[PassiveOpens]++;
			tcb->flags |= CLONE;
			tcpsetstate(s, Listen);
			break;

		case TCP_CONNECT:
			tpriv->stats[ActiveOpens]++;
			tcb->flags |= ACTIVE;
			tcpsndsyn(s, tcb);
			tcpsetstate(s, Syn_sent);
			tcpoutput(s);
			break;
	}
}

static char *tcpflag(uint16_t flag)
{
	static char buf[128];

	snprintf(buf, sizeof(buf), "%d", flag >> 10);	/* Head len */
	if (flag & URG)
		snprintf(buf, sizeof(buf), "%s%s", buf, " URG");
	if (flag & ACK)
		snprintf(buf, sizeof(buf), "%s%s", buf, " ACK");
	if (flag & PSH)
		snprintf(buf, sizeof(buf), "%s%s", buf, " PSH");
	if (flag & RST)
		snprintf(buf, sizeof(buf), "%s%s", buf, " RST");
	if (flag & SYN)
		snprintf(buf, sizeof(buf), "%s%s", buf, " SYN");
	if (flag & FIN)
		snprintf(buf, sizeof(buf), "%s%s", buf, " FIN");

	return buf;
}

/* Helper, determine if we should send a TCP timestamp.  ts_val was the
 * timestamp from our distant end.  We'll also send a TS on SYN (no ACK). */
static bool tcp_seg_has_ts(Tcp *tcph)
{
	return tcph->ts_val || ((tcph->flags & SYN) && !(tcph->flags & ACK));
}

/* Given a TCP header/segment and default header size (e.g. TCP4_HDRSIZE),
 * return the actual hdr_len and opt_pad */
static void compute_hdrlen_optpad(Tcp *tcph, uint16_t default_hdrlen,
                                  uint16_t *ret_hdrlen, uint16_t *ret_optpad,
                                  Tcpctl *tcb)
{
	uint16_t hdrlen = default_hdrlen;
	uint16_t optpad = 0;

	if (tcph->flags & SYN) {
		if (tcph->mss)
			hdrlen += MSS_LENGTH;
		if (tcph->ws)
			hdrlen += WS_LENGTH;
		if (tcph->sack_ok)
			hdrlen += SACK_OK_LENGTH;
	}
	if (tcp_seg_has_ts(tcph)) {
		hdrlen += TS_LENGTH;
		/* SYNs have other opts, don't do the PREPAD NOOP optimization. */
		if (!(tcph->flags & SYN))
			hdrlen += TS_SEND_PREPAD;
	}
	if (tcb && tcb->rcv.nr_sacks)
		hdrlen += 2 + tcb->rcv.nr_sacks * 8;
	optpad = hdrlen & 3;
	if (optpad)
		optpad = 4 - optpad;
	hdrlen += optpad;
	*ret_hdrlen = hdrlen;
	*ret_optpad = optpad;
}

/* Writes the TCP options for tcph to opt. */
static void write_opts(Tcp *tcph, uint8_t *opt, uint16_t optpad, Tcpctl *tcb)
{
	if (tcph->flags & SYN) {
		if (tcph->mss != 0) {
			*opt++ = MSSOPT;
			*opt++ = MSS_LENGTH;
			hnputs(opt, tcph->mss);
			opt += 2;
		}
		if (tcph->ws != 0) {
			*opt++ = WSOPT;
			*opt++ = WS_LENGTH;
			*opt++ = tcph->ws;
		}
		if (tcph->sack_ok) {
			*opt++ = SACK_OK_OPT;
			*opt++ = SACK_OK_LENGTH;
		}
	}
	if (tcp_seg_has_ts(tcph)) {
		if (!(tcph->flags & SYN)) {
			*opt++ = NOOPOPT;
			*opt++ = NOOPOPT;
		}
		*opt++ = TS_OPT;
		*opt++ = TS_LENGTH;
		/* Setting TSval, our time */
		hnputl(opt, milliseconds());
		opt += 4;
		/* Setting TSecr, the time we last saw from them, stored in ts_val */
		hnputl(opt, tcph->ts_val);
		opt += 4;
	}
	if (tcb && tcb->rcv.nr_sacks) {
		*opt++ = SACK_OPT;
		*opt++ = 2 + tcb->rcv.nr_sacks * 8;
		for (int i = 0; i < tcb->rcv.nr_sacks; i++) {
			hnputl(opt, tcb->rcv.sacks[i].left);
			opt += 4;
			hnputl(opt, tcb->rcv.sacks[i].right);
			opt += 4;
		}
	}
	while (optpad-- > 0)
		*opt++ = NOOPOPT;
}

/* Given a data block (or NULL) returns a block with enough header room that we
 * can send out.  block->wp is set to the beginning of the payload.  Returns
 * NULL on some sort of error. */
static struct block *alloc_or_pad_block(struct block *data,
                                        uint16_t total_hdr_size)
{
	if (data) {
		data = padblock(data, total_hdr_size);
		if (data == NULL)
			return NULL;
	} else {
		/* the 64 pad is to meet mintu's */
		data = block_alloc(total_hdr_size + 64, MEM_WAIT);
		if (data == NULL)
			return NULL;
		data->wp += total_hdr_size;
	}
	return data;
}

static struct block *htontcp6(Tcp *tcph, struct block *data, Tcp6hdr *ph,
                              Tcpctl *tcb)
{
	int dlen = blocklen(data);
	Tcp6hdr *h;
	uint16_t csum;
	uint16_t hdrlen, optpad;

	compute_hdrlen_optpad(tcph, TCP6_HDRSIZE, &hdrlen, &optpad, tcb);

	data = alloc_or_pad_block(data, hdrlen + TCP6_PKT);
	if (data == NULL)
		return NULL;
	/* relative to the block start (bp->rp).  Note TCP structs include IP. */
	data->network_offset = 0;
	data->transport_offset = offsetof(Tcp6hdr, tcpsport);

	/* copy in pseudo ip header plus port numbers */
	h = (Tcp6hdr *) (data->rp);
	memmove(h, ph, TCP6_TCBPHDRSZ);

	/* compose pseudo tcp header, do cksum calculation */
	hnputl(h->vcf, hdrlen + dlen);
	h->ploadlen[0] = h->ploadlen[1] = h->proto = 0;
	h->ttl = ph->proto;

	/* copy in variable bits */
	hnputl(h->tcpseq, tcph->seq);
	hnputl(h->tcpack, tcph->ack);
	hnputs(h->tcpflag, (hdrlen << 10) | tcph->flags);
	hnputs(h->tcpwin, tcph->wnd >> (tcb != NULL ? tcb->snd.scale : 0));
	hnputs(h->tcpurg, tcph->urg);

	write_opts(tcph, h->tcpopt, optpad, tcb);

	if (tcb != NULL && tcb->nochecksum) {
		h->tcpcksum[0] = h->tcpcksum[1] = 0;
	} else {
		csum = ptclcsum(data, TCP6_IPLEN, hdrlen + dlen + TCP6_PHDRSIZE);
		hnputs(h->tcpcksum, csum);
	}

	/* move from pseudo header back to normal ip header */
	memset(h->vcf, 0, 4);
	h->vcf[0] = IP_VER6;
	hnputs(h->ploadlen, hdrlen + dlen);
	h->proto = ph->proto;

	return data;
}

static struct block *htontcp4(Tcp *tcph, struct block *data, Tcp4hdr *ph,
                              Tcpctl *tcb)
{
	int dlen = blocklen(data);
	Tcp4hdr *h;
	uint16_t csum;
	uint16_t hdrlen, optpad;

	compute_hdrlen_optpad(tcph, TCP4_HDRSIZE, &hdrlen, &optpad, tcb);

	data = alloc_or_pad_block(data, hdrlen + TCP4_PKT);
	if (data == NULL)
		return NULL;
	/* relative to the block start (bp->rp).  Note TCP structs include IP. */
	data->network_offset = 0;
	data->transport_offset = offsetof(Tcp4hdr, tcpsport);

	/* copy in pseudo ip header plus port numbers */
	h = (Tcp4hdr *) (data->rp);
	memmove(h, ph, TCP4_TCBPHDRSZ);

	/* copy in variable bits */
	hnputs(h->tcplen, hdrlen + dlen);
	hnputl(h->tcpseq, tcph->seq);
	hnputl(h->tcpack, tcph->ack);
	hnputs(h->tcpflag, (hdrlen << 10) | tcph->flags);
	hnputs(h->tcpwin, tcph->wnd >> (tcb != NULL ? tcb->snd.scale : 0));
	hnputs(h->tcpurg, tcph->urg);

	write_opts(tcph, h->tcpopt, optpad, tcb);

	if (tcb != NULL && tcb->nochecksum) {
		h->tcpcksum[0] = h->tcpcksum[1] = 0;
	} else {
		assert(data->transport_offset == TCP4_IPLEN + TCP4_PHDRSIZE);
		csum = ~ptclcsum(data, TCP4_IPLEN, TCP4_PHDRSIZE);
		hnputs(h->tcpcksum, csum);
		data->tx_csum_offset = ph->tcpcksum - ph->tcpsport;
		data->flag |= Btcpck;
	}

	return data;
}

static void parse_inbound_sacks(Tcp *tcph, uint8_t *opt, uint16_t optlen)
{
	uint8_t nr_sacks;
	uint32_t left, right;

	nr_sacks = (optlen - 2) / 8;
	if (nr_sacks > MAX_NR_SACKS_PER_PACKET)
		return;
	opt += 2;
	for (int i = 0; i < nr_sacks; i++, opt += 8) {
		left = nhgetl(opt);
		right = nhgetl(opt + 4);
		if (seq_ge(left, right)) {
			/* bad / malicious SACK.  Skip it, and adjust. */
			nr_sacks--;
			i--;	/* stay on this array element next loop */
			continue;
		}
		tcph->sacks[i].left = left;
		tcph->sacks[i].right = right;
	}
	tcph->nr_sacks = nr_sacks;
}

static void parse_inbound_opts(Tcp *tcph, uint8_t *opt, uint16_t optsize)
{
	uint16_t optlen;

	while (optsize > 0 && *opt != EOLOPT) {
		if (*opt == NOOPOPT) {
			optsize--;
			opt++;
			continue;
		}
		optlen = opt[1];
		if (optlen < 2 || optlen > optsize)
			break;
		switch (*opt) {
			case MSSOPT:
				if (optlen == MSS_LENGTH)
					tcph->mss = nhgets(opt + 2);
				break;
			case WSOPT:
				if (optlen == WS_LENGTH && *(opt + 2) <= MAX_WS_VALUE)
					tcph->ws = HaveWS | *(opt + 2);
				break;
			case SACK_OK_OPT:
				if (optlen == SACK_OK_LENGTH)
					tcph->sack_ok = TRUE;
				break;
			case SACK_OPT:
				parse_inbound_sacks(tcph, opt, optlen);
				break;
			case TS_OPT:
				if (optlen == TS_LENGTH) {
					tcph->ts_val = nhgetl(opt + 2);
					tcph->ts_ecr = nhgetl(opt + 6);
				}
				break;
		}
		optsize -= optlen;
		opt += optlen;
	}
}

/* Helper, clears the opts.  We'll later set them with e.g. parse_inbound_opts,
 * set them manually, or something else. */
static void clear_tcph_opts(Tcp *tcph)
{
	tcph->mss = 0;
	tcph->ws = 0;
	tcph->sack_ok = FALSE;
	tcph->nr_sacks = 0;
	tcph->ts_val = 0;
	tcph->ts_ecr = 0;
}

static int ntohtcp6(Tcp *tcph, struct block **bpp)
{
	Tcp6hdr *h;
	uint16_t hdrlen;

	*bpp = pullupblock(*bpp, TCP6_PKT + TCP6_HDRSIZE);
	if (*bpp == NULL)
		return -1;

	h = (Tcp6hdr *) ((*bpp)->rp);
	tcph->source = nhgets(h->tcpsport);
	tcph->dest = nhgets(h->tcpdport);
	tcph->seq = nhgetl(h->tcpseq);
	tcph->ack = nhgetl(h->tcpack);
	hdrlen = (h->tcpflag[0] >> 2) & ~3;
	if (hdrlen < TCP6_HDRSIZE) {
		freeblist(*bpp);
		return -1;
	}

	tcph->flags = h->tcpflag[1];
	tcph->wnd = nhgets(h->tcpwin);
	tcph->urg = nhgets(h->tcpurg);
	clear_tcph_opts(tcph);
	tcph->len = nhgets(h->ploadlen) - hdrlen;

	*bpp = pullupblock(*bpp, hdrlen + TCP6_PKT);
	if (*bpp == NULL)
		return -1;
	parse_inbound_opts(tcph, h->tcpopt, hdrlen - TCP6_HDRSIZE);
	return hdrlen;
}

static int ntohtcp4(Tcp *tcph, struct block **bpp)
{
	Tcp4hdr *h;
	uint16_t hdrlen;

	*bpp = pullupblock(*bpp, TCP4_PKT + TCP4_HDRSIZE);
	if (*bpp == NULL)
		return -1;

	h = (Tcp4hdr *) ((*bpp)->rp);
	tcph->source = nhgets(h->tcpsport);
	tcph->dest = nhgets(h->tcpdport);
	tcph->seq = nhgetl(h->tcpseq);
	tcph->ack = nhgetl(h->tcpack);

	hdrlen = (h->tcpflag[0] >> 2) & ~3;
	if (hdrlen < TCP4_HDRSIZE) {
		freeblist(*bpp);
		return -1;
	}

	tcph->flags = h->tcpflag[1];
	tcph->wnd = nhgets(h->tcpwin);
	tcph->urg = nhgets(h->tcpurg);
	clear_tcph_opts(tcph);
	tcph->len = nhgets(h->length) - (hdrlen + TCP4_PKT);

	*bpp = pullupblock(*bpp, hdrlen + TCP4_PKT);
	if (*bpp == NULL)
		return -1;
	parse_inbound_opts(tcph, h->tcpopt, hdrlen - TCP4_HDRSIZE);
	return hdrlen;
}

/*
 *  For outgoing calls, generate an initial sequence
 *  number and put a SYN on the send queue
 */
static void tcpsndsyn(struct conv *s, Tcpctl *tcb)
{
	urandom_read(&tcb->iss, sizeof(tcb->iss));
	tcb->rttseq = tcb->iss;
	tcb->snd.wl2 = tcb->iss;
	tcb->snd.una = tcb->iss;
	tcb->snd.rtx = tcb->rttseq;
	tcb->snd.nxt = tcb->rttseq;
	tcb->flgcnt++;
	tcb->flags |= FORCE;
	tcb->sndsyntime = NOW;

	/* set desired mss and scale */
	tcb->mss = tcpmtu(tcb->ifc, s->ipversion, &tcb->scale);
}

static void sndrst(struct Proto *tcp, uint8_t *source, uint8_t *dest,
                   uint16_t length, Tcp *seg, uint8_t version, char *reason)
{
	struct block *hbp;
	uint8_t rflags;
	struct tcppriv *tpriv;
	Tcp4hdr ph4;
	Tcp6hdr ph6;

	netlog(tcp->f, Logtcpreset, "sndrst: %s\n", reason);

	tpriv = tcp->priv;

	if (seg->flags & RST)
		return;

	/* make pseudo header */
	switch (version) {
		case V4:
			memset(&ph4, 0, sizeof(ph4));
			ph4.vihl = IP_VER4;
			v6tov4(ph4.tcpsrc, dest);
			v6tov4(ph4.tcpdst, source);
			ph4.proto = IP_TCPPROTO;
			hnputs(ph4.tcplen, TCP4_HDRSIZE);
			hnputs(ph4.tcpsport, seg->dest);
			hnputs(ph4.tcpdport, seg->source);
			break;
		case V6:
			memset(&ph6, 0, sizeof(ph6));
			ph6.vcf[0] = IP_VER6;
			ipmove(ph6.tcpsrc, dest);
			ipmove(ph6.tcpdst, source);
			ph6.proto = IP_TCPPROTO;
			hnputs(ph6.ploadlen, TCP6_HDRSIZE);
			hnputs(ph6.tcpsport, seg->dest);
			hnputs(ph6.tcpdport, seg->source);
			break;
		default:
			panic("sndrst: version %d", version);
	}

	tpriv->stats[OutRsts]++;
	rflags = RST;

	/* convince the other end that this reset is in band */
	if (seg->flags & ACK) {
		seg->seq = seg->ack;
		seg->ack = 0;
	} else {
		rflags |= ACK;
		seg->ack = seg->seq;
		seg->seq = 0;
		if (seg->flags & SYN)
			seg->ack++;
		seg->ack += length;
		if (seg->flags & FIN)
			seg->ack++;
	}
	seg->flags = rflags;
	seg->wnd = 0;
	seg->urg = 0;
	seg->mss = 0;
	seg->ws = 0;
	seg->sack_ok = FALSE;
	seg->nr_sacks = 0;
	/* seg->ts_val is already set with their timestamp */
	switch (version) {
		case V4:
			hbp = htontcp4(seg, NULL, &ph4, NULL);
			if (hbp == NULL)
				return;
			ipoput4(tcp->f, hbp, 0, MAXTTL, DFLTTOS, NULL);
			break;
		case V6:
			hbp = htontcp6(seg, NULL, &ph6, NULL);
			if (hbp == NULL)
				return;
			ipoput6(tcp->f, hbp, 0, MAXTTL, DFLTTOS, NULL);
			break;
		default:
			panic("sndrst2: version %d", version);
	}
}

/*
 *  send a reset to the remote side and close the conversation
 *  called with s qlocked
 */
static void tcphangup(struct conv *s)
{
	ERRSTACK(1);
	Tcp seg;
	Tcpctl *tcb;
	struct block *hbp;

	tcb = (Tcpctl *) s->ptcl;
	if (ipcmp(s->raddr, IPnoaddr)) {
		/* discard error style, poperror regardless */
		if (!waserror()) {
			seg.flags = RST | ACK;
			seg.ack = tcb->rcv.nxt;
			tcb->last_ack_sent = seg.ack;
			tcb->rcv.una = 0;
			seg.seq = tcb->snd.nxt;
			seg.wnd = 0;
			seg.urg = 0;
			seg.mss = 0;
			seg.ws = 0;
			seg.sack_ok = FALSE;
			seg.nr_sacks = 0;
			seg.ts_val = tcb->ts_recent;
			switch (s->ipversion) {
				case V4:
					tcb->protohdr.tcp4hdr.vihl = IP_VER4;
					hbp = htontcp4(&seg, NULL, &tcb->protohdr.tcp4hdr, tcb);
					ipoput4(s->p->f, hbp, 0, s->ttl, s->tos, s);
					break;
				case V6:
					tcb->protohdr.tcp6hdr.vcf[0] = IP_VER6;
					hbp = htontcp6(&seg, NULL, &tcb->protohdr.tcp6hdr, tcb);
					ipoput6(s->p->f, hbp, 0, s->ttl, s->tos, s);
					break;
				default:
					panic("tcphangup: version %d", s->ipversion);
			}
		}
		poperror();
	}
	localclose(s, NULL);
}

/*
 *  (re)send a SYN ACK
 */
static int sndsynack(struct Proto *tcp, Limbo *lp)
{
	struct block *hbp;
	Tcp4hdr ph4;
	Tcp6hdr ph6;
	Tcp seg;
	int scale;
	uint8_t flag = 0;

	/* make pseudo header */
	switch (lp->version) {
		case V4:
			memset(&ph4, 0, sizeof(ph4));
			ph4.vihl = IP_VER4;
			v6tov4(ph4.tcpsrc, lp->laddr);
			v6tov4(ph4.tcpdst, lp->raddr);
			ph4.proto = IP_TCPPROTO;
			hnputs(ph4.tcplen, TCP4_HDRSIZE);
			hnputs(ph4.tcpsport, lp->lport);
			hnputs(ph4.tcpdport, lp->rport);
			break;
		case V6:
			memset(&ph6, 0, sizeof(ph6));
			ph6.vcf[0] = IP_VER6;
			ipmove(ph6.tcpsrc, lp->laddr);
			ipmove(ph6.tcpdst, lp->raddr);
			ph6.proto = IP_TCPPROTO;
			hnputs(ph6.ploadlen, TCP6_HDRSIZE);
			hnputs(ph6.tcpsport, lp->lport);
			hnputs(ph6.tcpdport, lp->rport);
			break;
		default:
			panic("sndrst: version %d", lp->version);
	}
	lp->ifc = findipifc(tcp->f, lp->laddr, 0);

	seg.seq = lp->iss;
	seg.ack = lp->irs + 1;
	seg.flags = SYN | ACK;
	seg.urg = 0;
	seg.mss = tcpmtu(lp->ifc, lp->version, &scale);
	seg.wnd = QMAX;
	seg.ts_val = lp->ts_val;
	seg.nr_sacks = 0;

	/* if the other side set scale, we should too */
	if (lp->rcvscale) {
		seg.ws = scale;
		lp->sndscale = scale;
	} else {
		seg.ws = 0;
		lp->sndscale = 0;
	}
	if (SACK_SUPPORTED)
		seg.sack_ok = lp->sack_ok;
	else
		seg.sack_ok = FALSE;

	switch (lp->version) {
		case V4:
			hbp = htontcp4(&seg, NULL, &ph4, NULL);
			if (hbp == NULL)
				return -1;
			ipoput4(tcp->f, hbp, 0, MAXTTL, DFLTTOS, NULL);
			break;
		case V6:
			hbp = htontcp6(&seg, NULL, &ph6, NULL);
			if (hbp == NULL)
				return -1;
			ipoput6(tcp->f, hbp, 0, MAXTTL, DFLTTOS, NULL);
			break;
		default:
			panic("sndsnack: version %d", lp->version);
	}
	lp->lastsend = NOW;
	return 0;
}

#define hashipa(a, p) ( ( (a)[IPaddrlen-2] + (a)[IPaddrlen-1] + p )&LHTMASK )

/*
 *  put a call into limbo and respond with a SYN ACK
 *
 *  called with proto locked
 */
static void limbo(struct conv *s, uint8_t *source, uint8_t *dest, Tcp *seg,
                  int version)
{
	Limbo *lp, **l;
	struct tcppriv *tpriv;
	int h;

	tpriv = s->p->priv;
	h = hashipa(source, seg->source);

	for (l = &tpriv->lht[h]; *l != NULL; l = &lp->next) {
		lp = *l;
		if (lp->lport != seg->dest || lp->rport != seg->source
			|| lp->version != version)
			continue;
		if (ipcmp(lp->raddr, source) != 0)
			continue;
		if (ipcmp(lp->laddr, dest) != 0)
			continue;

		/* each new SYN restarts the retransmits */
		lp->irs = seg->seq;
		break;
	}
	lp = *l;
	if (lp == NULL) {
		if (tpriv->nlimbo >= Maxlimbo && tpriv->lht[h]) {
			lp = tpriv->lht[h];
			tpriv->lht[h] = lp->next;
			lp->next = NULL;
		} else {
			lp = kzmalloc(sizeof(*lp), 0);
			if (lp == NULL)
				return;
			tpriv->nlimbo++;
		}
		*l = lp;
		lp->version = version;
		ipmove(lp->laddr, dest);
		ipmove(lp->raddr, source);
		lp->lport = seg->dest;
		lp->rport = seg->source;
		lp->mss = seg->mss;
		lp->rcvscale = seg->ws;
		lp->sack_ok = seg->sack_ok;
		lp->irs = seg->seq;
		lp->ts_val = seg->ts_val;
		urandom_read(&lp->iss, sizeof(lp->iss));
	}

	if (sndsynack(s->p, lp) < 0) {
		*l = lp->next;
		tpriv->nlimbo--;
		kfree(lp);
	}
}

/*
 *  resend SYN ACK's once every SYNACK_RXTIMER ms.
 */
static void limborexmit(struct Proto *tcp)
{
	struct tcppriv *tpriv;
	Limbo **l, *lp;
	int h;
	int seen;
	uint64_t now;

	tpriv = tcp->priv;

	if (!canqlock(&tcp->qlock))
		return;
	seen = 0;
	now = NOW;
	for (h = 0; h < NLHT && seen < tpriv->nlimbo; h++) {
		for (l = &tpriv->lht[h]; *l != NULL && seen < tpriv->nlimbo;) {
			lp = *l;
			seen++;
			if (now - lp->lastsend < (lp->rexmits + 1) * SYNACK_RXTIMER)
				continue;

			/* time it out after 1 second */
			if (++(lp->rexmits) > 5) {
				tpriv->nlimbo--;
				*l = lp->next;
				kfree(lp);
				continue;
			}

			/* if we're being attacked, don't bother resending SYN ACK's */
			if (tpriv->nlimbo > 100)
				continue;

			if (sndsynack(tcp, lp) < 0) {
				tpriv->nlimbo--;
				*l = lp->next;
				kfree(lp);
				continue;
			}

			l = &lp->next;
		}
	}
	qunlock(&tcp->qlock);
}

/*
 *  lookup call in limbo.  if found, throw it out.
 *
 *  called with proto locked
 */
static void limborst(struct conv *s, Tcp *segp, uint8_t *src, uint8_t *dst,
                     uint8_t version)
{
	Limbo *lp, **l;
	int h;
	struct tcppriv *tpriv;

	tpriv = s->p->priv;

	/* find a call in limbo */
	h = hashipa(src, segp->source);
	for (l = &tpriv->lht[h]; *l != NULL; l = &lp->next) {
		lp = *l;
		if (lp->lport != segp->dest || lp->rport != segp->source
			|| lp->version != version)
			continue;
		if (ipcmp(lp->laddr, dst) != 0)
			continue;
		if (ipcmp(lp->raddr, src) != 0)
			continue;

		/* RST can only follow the SYN */
		if (segp->seq == lp->irs + 1) {
			tpriv->nlimbo--;
			*l = lp->next;
			kfree(lp);
		}
		break;
	}
}

/* The advertised MSS (e.g. 1460) includes any per-packet TCP options, such as
 * TCP timestamps.  A given packet will contain mss bytes, but only typical_mss
 * bytes of *data*.  If we know we'll use those options, we should adjust our
 * typical_mss, which will affect the cwnd. */
static void adjust_typical_mss_for_opts(Tcp *tcph, Tcpctl *tcb)
{
	uint16_t opt_size = 0;

	if (tcph->ts_val)
		opt_size += TS_LENGTH + TS_SEND_PREPAD;
	opt_size = ROUNDUP(opt_size, 4);
	tcb->typical_mss -= opt_size;
}

/*
 *  come here when we finally get an ACK to our SYN-ACK.
 *  lookup call in limbo.  if found, create a new conversation
 *
 *  called with proto locked
 */
static struct conv *tcpincoming(struct conv *s, Tcp *segp, uint8_t *src,
								uint8_t *dst, uint8_t version)
{
	struct conv *new;
	Tcpctl *tcb;
	struct tcppriv *tpriv;
	Tcp4hdr *h4;
	Tcp6hdr *h6;
	Limbo *lp, **l;
	int h;

	/* unless it's just an ack, it can't be someone coming out of limbo */
	if ((segp->flags & SYN) || (segp->flags & ACK) == 0)
		return NULL;

	tpriv = s->p->priv;

	/* find a call in limbo */
	h = hashipa(src, segp->source);
	for (l = &tpriv->lht[h]; (lp = *l) != NULL; l = &lp->next) {
		netlog(s->p->f, Logtcp,
			   "tcpincoming s %I!%d/%I!%d d %I!%d/%I!%d v %d/%d\n", src,
			   segp->source, lp->raddr, lp->rport, dst, segp->dest, lp->laddr,
			   lp->lport, version, lp->version);

		if (lp->lport != segp->dest || lp->rport != segp->source
			|| lp->version != version)
			continue;
		if (ipcmp(lp->laddr, dst) != 0)
			continue;
		if (ipcmp(lp->raddr, src) != 0)
			continue;

		/* we're assuming no data with the initial SYN */
		if (segp->seq != lp->irs + 1 || segp->ack != lp->iss + 1) {
			netlog(s->p->f, Logtcp, "tcpincoming s 0x%lx/0x%lx a 0x%lx 0x%lx\n",
				   segp->seq, lp->irs + 1, segp->ack, lp->iss + 1);
			lp = NULL;
		} else {
			tpriv->nlimbo--;
			*l = lp->next;
		}
		break;
	}
	if (lp == NULL)
		return NULL;

	new = Fsnewcall(s, src, segp->source, dst, segp->dest, version);
	if (new == NULL)
		return NULL;

	memmove(new->ptcl, s->ptcl, sizeof(Tcpctl));
	tcb = (Tcpctl *) new->ptcl;
	tcb->flags &= ~CLONE;
	tcb->timer.arg = new;
	tcb->timer.state = TcptimerOFF;
	tcb->acktimer.arg = new;
	tcb->acktimer.state = TcptimerOFF;
	tcb->katimer.arg = new;
	tcb->katimer.state = TcptimerOFF;
	tcb->rtt_timer.arg = new;
	tcb->rtt_timer.state = TcptimerOFF;

	tcb->irs = lp->irs;
	tcb->rcv.nxt = tcb->irs + 1;
	tcb->rcv.urg = tcb->rcv.nxt;

	tcb->iss = lp->iss;
	tcb->rttseq = tcb->iss;
	tcb->snd.wl2 = tcb->iss;
	tcb->snd.una = tcb->iss + 1;
	tcb->snd.rtx = tcb->iss + 1;
	tcb->snd.nxt = tcb->iss + 1;
	tcb->flgcnt = 0;
	tcb->flags |= SYNACK;

	/* our sending max segment size cannot be bigger than what he asked for */
	if (lp->mss != 0 && lp->mss < tcb->mss) {
		tcb->mss = lp->mss;
		tcb->typical_mss = tcb->mss;
	}
	adjust_typical_mss_for_opts(segp, tcb);

	/* Here's where we record the previously-decided header options.  They were
	 * actually decided on when we agreed to them in the SYNACK we sent.  We
	 * didn't create an actual TCB until now, so we can copy those decisions out
	 * of the limbo tracker and into the TCB. */
	tcb->ifc = lp->ifc;
	tcb->sack_ok = lp->sack_ok;
	/* window scaling */
	tcpsetscale(new, tcb, lp->rcvscale, lp->sndscale);
	tcb_check_tso(tcb);

	tcb->snd.wnd = segp->wnd;
	tcb->cwind = tcb->typical_mss * CWIND_SCALE;

	/* set initial round trip time */
	tcb->sndsyntime = lp->lastsend + lp->rexmits * SYNACK_RXTIMER;
	tcpsynackrtt(new);

	kfree(lp);

	/* set up proto header */
	switch (version) {
		case V4:
			h4 = &tcb->protohdr.tcp4hdr;
			memset(h4, 0, sizeof(*h4));
			h4->proto = IP_TCPPROTO;
			hnputs(h4->tcpsport, new->lport);
			hnputs(h4->tcpdport, new->rport);
			v6tov4(h4->tcpsrc, dst);
			v6tov4(h4->tcpdst, src);
			break;
		case V6:
			h6 = &tcb->protohdr.tcp6hdr;
			memset(h6, 0, sizeof(*h6));
			h6->proto = IP_TCPPROTO;
			hnputs(h6->tcpsport, new->lport);
			hnputs(h6->tcpdport, new->rport);
			ipmove(h6->tcpsrc, dst);
			ipmove(h6->tcpdst, src);
			break;
		default:
			panic("tcpincoming: version %d", new->ipversion);
	}

	tcpsetstate(new, Established);

	iphtadd(&tpriv->ht, new);

	return new;
}

/*
 *  use the time between the first SYN and it's ack as the
 *  initial round trip time
 */
static void tcpsynackrtt(struct conv *s)
{
	Tcpctl *tcb;
	uint64_t delta;
	struct tcppriv *tpriv;

	tcb = (Tcpctl *) s->ptcl;
	tpriv = s->p->priv;

	delta = NOW - tcb->sndsyntime;
	tcb->srtt = delta;
	tcb->mdev = delta / 2;

	/* halt round trip timer */
	tcphalt(tpriv, &tcb->rtt_timer);
}

/* For LFNs (long/fat), our default tx queue doesn't hold enough data, and TCP
 * blocks on the application - even if the app already has the data ready to go.
 * We need to hold the sent, unacked data (1x cwnd), plus all the data we might
 * send next RTT (1x cwnd).  Note this is called after cwnd was expanded. */
static void adjust_tx_qio_limit(struct conv *s)
{
	Tcpctl *tcb = (Tcpctl *) s->ptcl;
	size_t ideal_limit = tcb->cwind * 2;

	/* This is called for every ACK, and it's not entirely free to update the
	 * limit (locks, CVs, taps).  Updating in chunks of mss seems reasonable.
	 * During SS, we'll update this on most ACKs (given each ACK increased the
	 * cwind by > MSS).
	 *
	 * We also don't want a lot of tiny blocks from the user, but the way qio
	 * works, you can put in as much as you want (Maxatomic) and then get
	 * flow-controlled. */
	if (qgetlimit(s->wq) + tcb->typical_mss < ideal_limit)
		qsetlimit(s->wq, ideal_limit);
	/* TODO: we could shrink the qio limit too, if we had a better idea what the
	 * actual threshold was.  We want the limit to be the 'stable' cwnd * 2. */
}

/* Attempts to merge later sacks into sack 'into' (index in the array) */
static void merge_sacks_into(Tcpctl *tcb, int into)
{
	struct sack_block *into_sack = &tcb->snd.sacks[into];
	struct sack_block *tcb_sack;
	int shift = 0;

	for (int i = into + 1; i < tcb->snd.nr_sacks; i++) {
		tcb_sack = &tcb->snd.sacks[i];
		if (seq_lt(into_sack->right, tcb_sack->left))
			break;
		if (seq_gt(tcb_sack->right, into_sack->right))
			into_sack->right = tcb_sack->right;
		shift++;
	}
	if (shift) {
		memmove(tcb->snd.sacks + into + 1,
		        tcb->snd.sacks + into + 1 + shift,
		        sizeof(struct sack_block) * (tcb->snd.nr_sacks - into - 1
				                             - shift));
		tcb->snd.nr_sacks -= shift;
	}
}

/* If we update a sack, it means they received a packet (possibly out of order),
 * but they have not received earlier packets.  Otherwise, they would do a full
 * ACK.
 *
 * The trick is in knowing whether the reception growing this sack is due to a
 * retrans or due to packets from before our last loss event.  The rightmost
 * sack tends to grow a lot with packets we sent before the loss.  However,
 * intermediate sacks that grow are signs of a loss, since they only grow as a
 * result of retrans.
 *
 * This is only true for the first time through a retrans.  After we've gone
 * through a full retrans blast, the sack that hinted at the retrans loss (and
 * there could be multiple of them!) will continue to grow.  We could come up
 * with some tracking for this, but instead we'll just do a one-time deal.  You
 * can recover from one detected sack retrans loss.  After that, you'll have to
 * use the RTO.
 *
 * This won't catch some things, like a sack that grew and merged with the
 * rightmost sack.  This also won't work if you have a single sack.  We can't
 * tell where the retrans ends and the sending begins. */
static bool sack_hints_at_loss(Tcpctl *tcb, struct sack_block *tcb_sack)
{
	if (tcb->snd.recovery != SACK_RETRANS_RECOVERY)
		return FALSE;
	return &tcb->snd.sacks[tcb->snd.nr_sacks - 1] != tcb_sack;
}

static bool sack_contains(struct sack_block *tcb_sack, uint32_t seq)
{
	return seq_le(tcb_sack->left, seq) && seq_lt(seq, tcb_sack->right);
}

/* Debugging helper! */
static void sack_asserter(Tcpctl *tcb, char *str)
{
	struct sack_block *tcb_sack;

	for (int i = 0; i < tcb->snd.nr_sacks; i++) {
		tcb_sack = &tcb->snd.sacks[i];
		/* Checking invariants: snd.rtx is never inside a sack, sacks are always
		 * mutually exclusive. */
		if (sack_contains(tcb_sack, tcb->snd.rtx) ||
		    ((i + 1 < tcb->snd.nr_sacks) && seq_ge(tcb_sack->right,
			                                       (tcb_sack + 1)->left))) {
			printk("SACK ASSERT ERROR at %s\n", str);
			printk("rtx %u una %u nxt %u, sack [%u, %u)\n",
			       tcb->snd.rtx, tcb->snd.una, tcb->snd.nxt, tcb_sack->left,
				   tcb_sack->right);
			for (int i = 0; i < tcb->snd.nr_sacks; i++)
				printk("\t %d: [%u, %u)\n", i, tcb->snd.sacks[i].left,
				       tcb->snd.sacks[i].right);
			backtrace();
			panic("");
		}
	}
}

/* Updates bookkeeping whenever a sack is added or updated */
static void sack_has_changed(struct conv *s, Tcpctl *tcb,
                             struct sack_block *tcb_sack)
{
	/* Due to the change, snd.rtx might be in the middle of this sack.  Advance
	 * it to the right edge. */
	if (sack_contains(tcb_sack, tcb->snd.rtx))
		tcb->snd.rtx = tcb_sack->right;

	/* This is a sack for something we retransed and we think it means there was
	 * another loss.  Instead of waiting for the RTO, we can take action. */
	if (sack_hints_at_loss(tcb, tcb_sack)) {
		if (++tcb->snd.sack_loss_hint == TCPREXMTTHRESH) {
			netlog(s->p->f, Logtcprxmt,
			       "%I.%d -> %I.%d: sack rxmit loss: snd.rtx %u, sack [%u,%u), una %u, recovery_pt %u\n",
			       s->laddr, s->lport, s->raddr, s->rport,
			       tcb->snd.rtx, tcb_sack->left, tcb_sack->right, tcb->snd.una,
			       tcb->snd.recovery_pt);
			/* Redo retrans, but keep the sacks and recovery point */
			tcp_loss_event(s, tcb);
			tcb->snd.rtx = tcb->snd.una;
			tcb->snd.sack_loss_hint = 0;
			/* Act like an RTO.  We just detected it earlier.  This prevents us
			 * from getting another sack hint loss this recovery period and from
			 * advancing the opportunistic right edge. */
			tcb->snd.recovery = RTO_RETRANS_RECOVERY;
			/* We didn't actually time out yet and we expect to keep getting
			 * sacks, so we don't want to flush or worry about in_flight.  If we
			 * messed something up, the RTO will still fire. */
			set_in_flight(tcb);
		}
	}
}

/* Advances tcb_sack's right edge, if new_right is farther, and updates the
 * bookkeeping due to the change. */
static void update_right_edge(struct conv *s, Tcpctl *tcb,
                              struct sack_block *tcb_sack, uint32_t new_right)
{
	if (seq_le(new_right, tcb_sack->right))
		return;
	tcb_sack->right = new_right;
	merge_sacks_into(tcb, tcb_sack - tcb->snd.sacks);
	sack_has_changed(s, tcb, tcb_sack);
}

static void update_or_insert_sack(struct conv *s, Tcpctl *tcb,
                                  struct sack_block *seg_sack)
{
	struct sack_block *tcb_sack;

	for (int i = 0; i < tcb->snd.nr_sacks; i++) {
		tcb_sack = &tcb->snd.sacks[i];
		if (seq_lt(tcb_sack->left, seg_sack->left)) {
			/* This includes adjacent (which I've seen!) and overlap. */
			if (seq_le(seg_sack->left, tcb_sack->right)) {
				update_right_edge(s, tcb, tcb_sack, seg_sack->right);
				return;
			}
			continue;
		}
		/* Update existing sack */
		if (tcb_sack->left == seg_sack->left) {
			update_right_edge(s, tcb, tcb_sack, seg_sack->right);
			return;
		}
		/* Found our slot */
		if (seq_gt(tcb_sack->left, seg_sack->left)) {
			if (tcb->snd.nr_sacks == MAX_NR_SND_SACKS) {
				/* Out of room, but it is possible this sack overlaps later
				 * sacks, including the max sack's right edge. */
				if (seq_ge(seg_sack->right, tcb_sack->left)) {
					/* Take over the sack */
					tcb_sack->left = seg_sack->left;
					update_right_edge(s, tcb, tcb_sack, seg_sack->right);
				}
				return;
			}
			/* O/W, it's our slot and we have room (at least one spot). */
			memmove(&tcb->snd.sacks[i + 1], &tcb->snd.sacks[i],
			        sizeof(struct sack_block) * (tcb->snd.nr_sacks - i));
			tcb_sack->left = seg_sack->left;
			tcb_sack->right = seg_sack->right;
			tcb->snd.nr_sacks++;
			merge_sacks_into(tcb, i);
			sack_has_changed(s, tcb, tcb_sack);
			return;
		}
	}
	if (tcb->snd.nr_sacks == MAX_NR_SND_SACKS) {
		/* We didn't find space in the sack array. */
		tcb_sack = &tcb->snd.sacks[MAX_NR_SND_SACKS - 1];
		/* Need to always maintain the rightmost sack, discarding the prev */
		if (seq_gt(seg_sack->right, tcb_sack->right)) {
			tcb_sack->left = seg_sack->left;
			tcb_sack->right = seg_sack->right;
			sack_has_changed(s, tcb, tcb_sack);
		}
		return;
	}
	tcb_sack = &tcb->snd.sacks[tcb->snd.nr_sacks];
	tcb->snd.nr_sacks++;
	tcb_sack->left = seg_sack->left;
	tcb_sack->right = seg_sack->right;
	sack_has_changed(s, tcb, tcb_sack);
}

/* Given the packet seg, track the sacks in TCB.  There are a few things: if seg
 * acks new data, some sacks might no longer be needed.  Some sacks might grow,
 * we might add new sacks, either of which can cause a merger.
 *
 * The important thing is that we always have the max sack entry: it must be
 * inserted for sure and findable.  We need that for our measurement of what
 * packets are in the network.
 *
 * Note that we keep sacks that are below snd.rtx (and above
 * seg.ack/tcb->snd.una) as best we can - we don't prune them.  We'll need those
 * for the in_flight estimate.
 *
 * When we run out of room, we'll have to throw away a sack.  Anything we throw
 * away below snd.rtx will be counted as 'in flight', even though it isn't.  If
 * we throw away something greater than snd.rtx, we'll also retrans it.  For
 * simplicity, we throw-away / replace the rightmost sack, since we're always
 * maintaining a highest sack. */
static void update_sacks(struct conv *s, Tcpctl *tcb, Tcp *seg)
{
	int prune = 0;
	struct sack_block *tcb_sack;

	for (int i = 0; i < tcb->snd.nr_sacks; i++) {
		tcb_sack = &tcb->snd.sacks[i];
		/* For the equality case, if they acked up to, but not including an old
		 * sack, they must have reneged it.  Otherwise they would have acked
		 * beyond the sack. */
		if (seq_lt(seg->ack, tcb_sack->left))
			break;
		prune++;
	}
	if (prune) {
		memmove(tcb->snd.sacks, tcb->snd.sacks + prune,
		        sizeof(struct sack_block) * (tcb->snd.nr_sacks - prune));
		tcb->snd.nr_sacks -= prune;
	}
	for (int i = 0; i < seg->nr_sacks; i++) {
		/* old sacks */
		if (seq_lt(seg->sacks[i].left, seg->ack))
			continue;
		/* buggy sack: out of range */
		if (seq_gt(seg->sacks[i].right, tcb->snd.nxt))
			continue;
		update_or_insert_sack(s, tcb, &seg->sacks[i]);
	}
}

/* This is a little bit of an under estimate, since we assume a packet is lost
 * once we have any sacks above it.  Overall, it's at most 2 * MSS of an
 * overestimate.
 *
 * If we have no sacks (either reneged or never used) we'll assume all packets
 * above snd.rtx are lost.  This will be the case for sackless fast rxmit
 * (Dong's stuff) or for a timeout.  In the former case, this is probably not
 * true, and in_flight should be higher, but we have no knowledge without the
 * sacks. */
static void set_in_flight(Tcpctl *tcb)
{
	struct sack_block *tcb_sack;
	uint32_t in_flight = 0;
	uint32_t from;

	if (!tcb->snd.nr_sacks) {
		tcb->snd.in_flight = tcb->snd.rtx - tcb->snd.una;
		return;
	}

	/* Everything to the right of the unsacked */
	tcb_sack = &tcb->snd.sacks[tcb->snd.nr_sacks - 1];
	in_flight += tcb->snd.nxt - tcb_sack->right;

	/* Everything retransed (from una to snd.rtx, minus sacked regions.  Note
	 * we only retrans at most the last sack's left edge.  snd.rtx will be
	 * advanced to the right edge of some sack (possibly the last one). */
	from = tcb->snd.una;
	for (int i = 0; i < tcb->snd.nr_sacks; i++) {
		tcb_sack = &tcb->snd.sacks[i];
		if (seq_ge(tcb_sack->left, tcb->snd.rtx))
			break;
		assert(seq_ge(tcb->snd.rtx, tcb_sack->right));
		in_flight += tcb_sack->left - from;
		from = tcb_sack->right;
	}
	in_flight += tcb->snd.rtx - from;

	tcb->snd.in_flight = in_flight;
}

static void reset_recovery(struct conv *s, Tcpctl *tcb)
{
	netlog(s->p->f, Logtcprxmt,
	       "%I.%d -> %I.%d: recovery complete, una %u, rtx %u, nxt %u, recovery %u\n",
	       s->laddr, s->lport, s->raddr, s->rport,
	       tcb->snd.una, tcb->snd.rtx, tcb->snd.nxt, tcb->snd.recovery_pt);
	tcb->snd.recovery = 0;
	tcb->snd.recovery_pt = 0;
	tcb->snd.loss_hint = 0;
	tcb->snd.flush_sacks = FALSE;
	tcb->snd.sack_loss_hint = 0;
}

static bool is_dup_ack(Tcpctl *tcb, Tcp *seg)
{
	/* this is a pure ack w/o window update */
	return (seg->ack == tcb->snd.una) &&
	       (tcb->snd.una != tcb->snd.nxt) &&
	       (seg->len == 0) &&
	       (seg->wnd == tcb->snd.wnd);
}

/* If we have sacks, we'll ignore dupacks and look at the sacks ahead of una
 * (which are managed by the TCB).  The tcb will not have old sacks (below
 * ack/snd.rtx).  Receivers often send sacks below their ack point when we are
 * coming out of a loss, and we don't want those to count.
 *
 * Note the tcb could have sacks (in the future), but the receiver stopped using
 * them (reneged).  We'll catch that with the RTO.  If we try to catch it here,
 * we could get in a state where we never allow them to renege. */
static bool is_potential_loss(Tcpctl *tcb, Tcp *seg)
{
	if (seg->nr_sacks > 0)
		return tcb->snd.nr_sacks > 0;
	else
		return is_dup_ack(tcb, seg);
}

/* When we use timestamps for RTTM, RFC 7323 suggests scaling by
 * expected_samples (per cwnd).  They say:
 *
 * ExpectedSamples = ceiling(FlightSize / (SMSS * 2))
 *
 * However, SMMS * 2 is really "number of bytes expected to be acked in a
 * packet.".  We'll use 'acked' to approximate that.  When the receiver uses
 * LRO, they'll send back large ACKs, which decreases the number of samples.
 *
 * If it turns out that all the divides are bad, we can just go back to not
 * using expected_samples at all. */
static int expected_samples_ts(Tcpctl *tcb, uint32_t acked)
{
	assert(acked);
	return MAX(DIV_ROUND_UP(tcb->snd.nxt - tcb->snd.una, acked), 1);
}

/* Updates the RTT, given the currently sampled RTT and the number samples per
 * cwnd.  For non-TS RTTM, that'll be 1. */
static void update_rtt(Tcpctl *tcb, int rtt_sample, int expected_samples)
{
	int delta;

	tcb->backoff = 0;
	tcb->backedoff = 0;
	if (tcb->srtt == 0) {
		tcb->srtt = rtt_sample;
		tcb->mdev = rtt_sample / 2;
	} else {
		delta = rtt_sample - tcb->srtt;
		tcb->srtt += (delta >> RTTM_ALPHA_SHIFT) / expected_samples;
		if (tcb->srtt <= 0)
			tcb->srtt = 1;
		tcb->mdev += ((abs(delta) - tcb->mdev) >> RTTM_BRAVO_SHIFT) /
		             expected_samples;
		if (tcb->mdev <= 0)
			tcb->mdev = 1;
	}
	tcpsettimer(tcb);
}

static void update(struct conv *s, Tcp *seg)
{
	int rtt;
	Tcpctl *tcb;
	uint32_t acked, expand;
	struct tcppriv *tpriv;

	tpriv = s->p->priv;
	tcb = (Tcpctl *) s->ptcl;

	if (!seq_within(seg->ack, tcb->snd.una, tcb->snd.nxt))
		return;

	acked = seg->ack - tcb->snd.una;
	tcb->snd.una = seg->ack;
	if (seq_gt(seg->ack, tcb->snd.rtx))
		tcb->snd.rtx = seg->ack;

	update_sacks(s, tcb, seg);
	set_in_flight(tcb);

	/* We treat either a dupack or forward SACKs as a hint that there is a loss.
	 * The RFCs suggest three dupacks before treating it as a loss (alternative
	 * is reordered packets).  We'll treat three SACKs the same way. */
	if (is_potential_loss(tcb, seg) && !tcb->snd.recovery) {
		tcb->snd.loss_hint++;
		if (tcb->snd.loss_hint == TCPREXMTTHRESH) {
			netlog(s->p->f, Logtcprxmt,
			       "%I.%d -> %I.%d: loss hint thresh, nr sacks %u, nxt %u, una %u, cwnd %u\n",
			       s->laddr, s->lport, s->raddr, s->rport,
			       tcb->snd.nr_sacks, tcb->snd.nxt, tcb->snd.una, tcb->cwind);
			tcp_loss_event(s, tcb);
			tcb->snd.recovery_pt = tcb->snd.nxt;
			if (tcb->snd.nr_sacks) {
				tcb->snd.recovery = SACK_RETRANS_RECOVERY;
				tcb->snd.flush_sacks = FALSE;
				tcb->snd.sack_loss_hint = 0;
			} else {
				tcb->snd.recovery = FAST_RETRANS_RECOVERY;
			}
			tcprxmit(s);
		}
	}

	/*
	 *  update window
	 */
	if (seq_gt(seg->ack, tcb->snd.wl2)
		|| (tcb->snd.wl2 == seg->ack && seg->wnd > tcb->snd.wnd)) {
		tcb->snd.wnd = seg->wnd;
		tcb->snd.wl2 = seg->ack;
	}

	if (!acked) {
		/*
		 *  don't let us hangup if sending into a closed window and
		 *  we're still getting acks
		 */
		if (tcb->snd.recovery && (tcb->snd.wnd == 0))
			tcb->backedoff = MAXBACKMS / 4;
		return;
	}
	/* At this point, they have acked something new. (positive ack, ack > una).
	 *
	 * If we hadn't reached the threshold for recovery yet, the positive ACK
	 * will reset our loss_hint count. */
	if (!tcb->snd.recovery)
		tcb->snd.loss_hint = 0;
	else if (seq_ge(seg->ack, tcb->snd.recovery_pt))
		reset_recovery(s, tcb);

	/* avoid slow start and timers for SYN acks */
	if ((tcb->flags & SYNACK) == 0) {
		tcb->flags |= SYNACK;
		acked--;
		tcb->flgcnt--;
		goto done;
	}

	/* slow start as long as we're not recovering from lost packets */
	if (tcb->cwind < tcb->snd.wnd && !tcb->snd.recovery) {
		if (tcb->cwind < tcb->ssthresh) {
			/* We increase the cwind by every byte we receive.  We want to
			 * increase the cwind by one MSS for every MSS that gets ACKed.
			 * Note that multiple MSSs can be ACKed in a single ACK.  If we had
			 * a remainder of acked / MSS, we'd add just that remainder - not 0
			 * or 1 MSS. */
			expand = acked;
		} else {
			/* Every RTT, which consists of CWND bytes, we're supposed to expand
			 * by MSS bytes.  The classic algorithm was
			 * 		expand = (tcb->mss * tcb->mss) / tcb->cwind;
			 * which assumes the ACK was for MSS bytes.  Instead, for every
			 * 'acked' bytes, we increase the window by acked / CWND (in units
			 * of MSS). */
			expand = MAX(acked, tcb->typical_mss) * tcb->typical_mss
			         / tcb->cwind;
		}

		if (tcb->cwind + expand < tcb->cwind)
			expand = tcb->snd.wnd - tcb->cwind;
		if (tcb->cwind + expand > tcb->snd.wnd)
			expand = tcb->snd.wnd - tcb->cwind;
		tcb->cwind += expand;
	}
	adjust_tx_qio_limit(s);

	if (tcb->ts_recent) {
		update_rtt(tcb, abs(milliseconds() - seg->ts_ecr),
		           expected_samples_ts(tcb, acked));
	} else if (tcb->rtt_timer.state == TcptimerON &&
	           seq_ge(seg->ack, tcb->rttseq)) {
		/* Adjust the timers according to the round trip time */
		tcphalt(tpriv, &tcb->rtt_timer);
		if (!tcb->snd.recovery) {
			rtt = tcb->rtt_timer.start - tcb->rtt_timer.count;
			if (rtt == 0)
				rtt = 1;	/* o/w all close systems will rexmit in 0 time */
			rtt *= MSPTICK;
			update_rtt(tcb, rtt, 1);
		}
	}

done:
	if (qdiscard(s->wq, acked) < acked) {
		tcb->flgcnt--;
		/* This happened due to another bug where acked was very large
		 * (negative), which was interpreted as "hey, one less flag, since they
		 * acked one of our flags (like a SYN).  If flgcnt goes negative,
		 * get_xmit_segment() will attempt to send out large packets. */
		assert(tcb->flgcnt >= 0);
	}

	if (seq_gt(seg->ack, tcb->snd.urg))
		tcb->snd.urg = seg->ack;

	if (tcb->snd.una != tcb->snd.nxt)
		tcpgo(tpriv, &tcb->timer);
	else
		tcphalt(tpriv, &tcb->timer);

	tcb->backoff = 0;
	tcb->backedoff = 0;
}

static void update_tcb_ts(Tcpctl *tcb, Tcp *seg)
{
	/* Get timestamp info from the tcp header.  Even though the timestamps
	 * aren't sequence numbers, we still need to protect for wraparound.  Though
	 * if the values were 0, assume that means we need an update.  We could have
	 * an initial ts_val that appears negative (signed). */
	if (!tcb->ts_recent || !tcb->last_ack_sent ||
	    (seq_ge(seg->ts_val, tcb->ts_recent) &&
	     seq_le(seg->seq, tcb->last_ack_sent)))
		tcb->ts_recent = seg->ts_val;
}

/* Overlap happens when one sack's left edge is inside another sack. */
static bool sacks_overlap(struct sack_block *x, struct sack_block *y)
{
	return (seq_le(x->left, y->left) && seq_le(y->left, x->right)) ||
	       (seq_le(y->left, x->left) && seq_le(x->left, y->right));
}

static void make_sack_first(Tcpctl *tcb, struct sack_block *tcb_sack)
{
	struct sack_block temp;

	if (tcb_sack == &tcb->rcv.sacks[0])
		return;
	temp = tcb->rcv.sacks[0];
	tcb->rcv.sacks[0] = *tcb_sack;
	*tcb_sack = temp;
}

/* Track sack in our tcb for a block of data we received.  This handles all the
 * stuff: making sure sack is first (since it's the most recent sack change),
 * updating or merging sacks, and dropping excess sacks (we only need to
 * maintain 3).  Unlike on the snd side, our tcb sacks are *not* sorted. */
static void track_rcv_sack(Tcpctl *tcb, uint32_t left, uint32_t right)
{
	struct sack_block *tcb_sack;
	struct sack_block sack[1];

	if (!tcb->sack_ok)
		return;
	assert(seq_lt(left, right));
	sack->left = left;
	sack->right = right;
	/* We can reuse an existing sack if we're merging or overlapping. */
	for (int i = 0; i < tcb->rcv.nr_sacks; i++) {
		tcb_sack = &tcb->rcv.sacks[i];
		if (sacks_overlap(tcb_sack, sack)) {
			tcb_sack->left = seq_min(tcb_sack->left, sack->left);
			tcb_sack->right = seq_max(tcb_sack->right, sack->right);
			make_sack_first(tcb, tcb_sack);
			return;
		}
	}
	/* We can discard the last sack (right shift) - we should have sent it at
	 * least once by now.  If not, oh well. */
	memmove(tcb->rcv.sacks + 1, tcb->rcv.sacks, sizeof(struct sack_block) *
	        MIN(MAX_NR_RCV_SACKS - 1, tcb->rcv.nr_sacks));
	tcb->rcv.sacks[0] = *sack;
	if (tcb->rcv.nr_sacks < MAX_NR_RCV_SACKS)
		tcb->rcv.nr_sacks++;
}

/* Once we receive everything and move rcv.nxt past a sack, we don't need to
 * track it.  I've seen Linux report sacks in the past, but we probably
 * shouldn't. */
static void drop_old_rcv_sacks(Tcpctl *tcb)
{
	struct sack_block *tcb_sack;

	for (int i = 0; i < tcb->rcv.nr_sacks; i++) {
		tcb_sack = &tcb->rcv.sacks[i];
		/* Moving up to or past the left is enough to drop it. */
		if (seq_ge(tcb->rcv.nxt, tcb_sack->left)) {
			memmove(tcb->rcv.sacks + i, tcb->rcv.sacks + i + 1,
			        sizeof(struct sack_block) * (tcb->rcv.nr_sacks - i - 1));
			tcb->rcv.nr_sacks--;
			i--;
		}
	}
}

static void tcpiput(struct Proto *tcp, struct Ipifc *unused, struct block *bp)
{
	ERRSTACK(1);
	Tcp seg;
	Tcp4hdr *h4;
	Tcp6hdr *h6;
	int hdrlen;
	Tcpctl *tcb;
	uint16_t length;
	uint8_t source[IPaddrlen], dest[IPaddrlen];
	struct conv *s;
	struct Fs *f;
	struct tcppriv *tpriv;
	uint8_t version;

	f = tcp->f;
	tpriv = tcp->priv;

	tpriv->stats[InSegs]++;

	h4 = (Tcp4hdr *) (bp->rp);
	h6 = (Tcp6hdr *) (bp->rp);

	if ((h4->vihl & 0xF0) == IP_VER4) {
		uint8_t ttl;

		version = V4;
		length = nhgets(h4->length);
		v4tov6(dest, h4->tcpdst);
		v4tov6(source, h4->tcpsrc);

		/* ttl isn't part of the xsum pseudo header, but bypass needs it. */
		ttl = h4->Unused;
		h4->Unused = 0;
		hnputs(h4->tcplen, length - TCP4_PKT);
		if (!(bp->flag & Btcpck) && (h4->tcpcksum[0] || h4->tcpcksum[1]) &&
			ptclcsum(bp, TCP4_IPLEN, length - TCP4_IPLEN)) {
			tpriv->stats[CsumErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcp proto cksum\n");
			freeblist(bp);
			return;
		}
		h4->Unused = ttl;

		hdrlen = ntohtcp4(&seg, &bp);
		if (hdrlen < 0) {
			tpriv->stats[HlenErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcp hdr len\n");
			return;
		}

		s = iphtlook(&tpriv->ht, source, seg.source, dest, seg.dest);
		if (s && s->state == Bypass) {
			bypass_or_drop(s, bp);
			return;
		}

		/* trim the packet to the size claimed by the datagram */
		length -= hdrlen + TCP4_PKT;
		bp = trimblock(bp, hdrlen + TCP4_PKT, length);
		if (bp == NULL) {
			tpriv->stats[LenErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "tcp len < 0 after trim\n");
			return;
		}
	} else {
		int ttl = h6->ttl;
		int proto = h6->proto;

		version = V6;
		length = nhgets(h6->ploadlen);
		ipmove(dest, h6->tcpdst);
		ipmove(source, h6->tcpsrc);

		h6->ploadlen[0] = h6->ploadlen[1] = h6->proto = 0;
		h6->ttl = proto;
		hnputl(h6->vcf, length);
		if ((h6->tcpcksum[0] || h6->tcpcksum[1]) &&
			ptclcsum(bp, TCP6_IPLEN, length + TCP6_PHDRSIZE)) {
			tpriv->stats[CsumErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcp proto cksum\n");
			freeblist(bp);
			return;
		}
		h6->ttl = ttl;
		h6->proto = proto;
		hnputs(h6->ploadlen, length);

		hdrlen = ntohtcp6(&seg, &bp);
		if (hdrlen < 0) {
			tpriv->stats[HlenErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcp hdr len\n");
			return;
		}

		s = iphtlook(&tpriv->ht, source, seg.source, dest, seg.dest);
		if (s && s->state == Bypass) {
			bypass_or_drop(s, bp);
			return;
		}

		/* trim the packet to the size claimed by the datagram */
		length -= hdrlen;
		bp = trimblock(bp, hdrlen + TCP6_PKT, length);
		if (bp == NULL) {
			tpriv->stats[LenErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "tcp len < 0 after trim\n");
			return;
		}
	}

	/* s, the conv matching the n-tuple, was set above */
	if (s == NULL) {
		netlog(f, Logtcpreset, "iphtlook failed: src %I:%u, dst %I:%u\n",
		       source, seg.source, dest, seg.dest);
reset:
		sndrst(tcp, source, dest, length, &seg, version, "no conversation");
		freeblist(bp);
		return;
	}

	/* lock protocol for unstate Plan 9 invariants.  funcs like limbo or
	 * incoming might rely on it. */
	qlock(&tcp->qlock);

	/* if it's a listener, look for the right flags and get a new conv */
	tcb = (Tcpctl *) s->ptcl;
	if (tcb->state == Listen) {
		if (seg.flags & RST) {
			limborst(s, &seg, source, dest, version);
			qunlock(&tcp->qlock);
			freeblist(bp);
			return;
		}

		/* if this is a new SYN, put the call into limbo */
		if ((seg.flags & SYN) && (seg.flags & ACK) == 0) {
			limbo(s, source, dest, &seg, version);
			qunlock(&tcp->qlock);
			freeblist(bp);
			return;
		}

		/* if there's a matching call in limbo, tcpincoming will return it */
		s = tcpincoming(s, &seg, source, dest, version);
		if (s == NULL) {
			qunlock(&tcp->qlock);
			goto reset;
		}
	}

	/* The rest of the input state machine is run with the control block
	 * locked and implements the state machine directly out of the RFC.
	 * Out-of-band data is ignored - it was always a bad idea.
	 */
	tcb = (Tcpctl *) s->ptcl;
	if (waserror()) {
		qunlock(&s->qlock);
		nexterror();
	}
	qlock(&s->qlock);
	qunlock(&tcp->qlock);

	update_tcb_ts(tcb, &seg);
	/* fix up window */
	seg.wnd <<= tcb->rcv.scale;

	/* every input packet in puts off the keep alive time out */
	tcpsetkacounter(tcb);

	switch (tcb->state) {
		case Closed:
			sndrst(tcp, source, dest, length, &seg, version,
				   "sending to Closed");
			goto raise;
		case Syn_sent:
			if (seg.flags & ACK) {
				if (!seq_within(seg.ack, tcb->iss + 1, tcb->snd.nxt)) {
					sndrst(tcp, source, dest, length, &seg, version,
						   "bad seq in Syn_sent");
					goto raise;
				}
			}
			if (seg.flags & RST) {
				if (seg.flags & ACK)
					localclose(s, "connection refused");
				goto raise;
			}

			if (seg.flags & SYN) {
				procsyn(s, &seg);
				if (seg.flags & ACK) {
					update(s, &seg);
					tcpsynackrtt(s);
					tcpsetstate(s, Established);
					/* Here's where we get the results of header option
					 * negotiations for connections we started. (SYNACK has the
					 * response) */
					tcpsetscale(s, tcb, seg.ws, tcb->scale);
					tcb->sack_ok = seg.sack_ok;
				} else {
					sndrst(tcp, source, dest, length, &seg, version,
						   "Got SYN with no ACK");
					goto raise;
				}

				if (length != 0 || (seg.flags & FIN))
					break;

				freeblist(bp);
				goto output;
			} else
				freeblist(bp);

			qunlock(&s->qlock);
			poperror();
			return;
	}

	/*
	 *  One DOS attack is to open connections to us and then forget about them,
	 *  thereby tying up a conv at no long term cost to the attacker.
	 *  This is an attempt to defeat these stateless DOS attacks.  See
	 *  corresponding code in tcpsendka().
	 */
	if ((seg.flags & RST) == 0) {
		if (tcpporthogdefense
			&& seq_within(seg.ack, tcb->snd.una - (1 << 31),
						  tcb->snd.una - (1 << 29))) {
			printd("stateless hog %I.%d->%I.%d f 0x%x 0x%lx - 0x%lx - 0x%lx\n",
				   source, seg.source, dest, seg.dest, seg.flags,
				   tcb->snd.una - (1 << 31), seg.ack, tcb->snd.una - (1 << 29));
			localclose(s, "stateless hog");
		}
	}

	/* Cut the data to fit the receive window */
	if (tcptrim(tcb, &seg, &bp, &length) == -1) {
		netlog(f, Logtcp, "%I.%d -> %I.%d: tcp len < 0, %lu %d\n",
		       s->raddr, s->rport, s->laddr, s->lport, seg.seq, length);
		update(s, &seg);
		if (qlen(s->wq) + tcb->flgcnt == 0 && tcb->state == Closing) {
			tcphalt(tpriv, &tcb->rtt_timer);
			tcphalt(tpriv, &tcb->acktimer);
			tcphalt(tpriv, &tcb->katimer);
			tcpsetstate(s, Time_wait);
			tcb->timer.start = MSL2 * (1000 / MSPTICK);
			tcpgo(tpriv, &tcb->timer);
		}
		if (!(seg.flags & RST)) {
			tcb->flags |= FORCE;
			goto output;
		}
		qunlock(&s->qlock);
		poperror();
		return;
	}

	/* Cannot accept so answer with a rst */
	if (length && tcb->state == Closed) {
		sndrst(tcp, source, dest, length, &seg, version, "sending to Closed");
		goto raise;
	}

	/* The segment is beyond the current receive pointer so
	 * queue the data in the resequence queue
	 */
	if (seg.seq != tcb->rcv.nxt)
		if (length != 0 || (seg.flags & (SYN | FIN))) {
			update(s, &seg);
			if (addreseq(tcb, tpriv, &seg, bp, length) < 0)
				printd("reseq %I.%d -> %I.%d\n", s->raddr, s->rport, s->laddr,
					   s->lport);
			tcb->flags |= FORCE;
			goto output;
		}

	/*
	 *  keep looping till we've processed this packet plus any
	 *  adjacent packets in the resequence queue
	 */
	for (;;) {
		if (seg.flags & RST) {
			if (tcb->state == Established) {
				tpriv->stats[EstabResets]++;
				if (tcb->rcv.nxt != seg.seq)
					printd
						("out of order RST rcvd: %I.%d -> %I.%d, rcv.nxt 0x%lx seq 0x%lx\n",
						 s->raddr, s->rport, s->laddr, s->lport, tcb->rcv.nxt,
						 seg.seq);
			}
			localclose(s, "connection refused");
			goto raise;
		}

		if ((seg.flags & ACK) == 0)
			goto raise;

		switch (tcb->state) {
			case Established:
			case Close_wait:
				update(s, &seg);
				break;
			case Finwait1:
				update(s, &seg);
				if (qlen(s->wq) + tcb->flgcnt == 0) {
					tcphalt(tpriv, &tcb->rtt_timer);
					tcphalt(tpriv, &tcb->acktimer);
					tcpsetkacounter(tcb);
					tcb->time = NOW;
					tcpsetstate(s, Finwait2);
					tcb->katimer.start = MSL2 * (1000 / MSPTICK);
					tcpgo(tpriv, &tcb->katimer);
				}
				break;
			case Finwait2:
				update(s, &seg);
				break;
			case Closing:
				update(s, &seg);
				if (qlen(s->wq) + tcb->flgcnt == 0) {
					tcphalt(tpriv, &tcb->rtt_timer);
					tcphalt(tpriv, &tcb->acktimer);
					tcphalt(tpriv, &tcb->katimer);
					tcpsetstate(s, Time_wait);
					tcb->timer.start = MSL2 * (1000 / MSPTICK);
					tcpgo(tpriv, &tcb->timer);
				}
				break;
			case Last_ack:
				update(s, &seg);
				if (qlen(s->wq) + tcb->flgcnt == 0) {
					localclose(s, NULL);
					goto raise;
				}
			case Time_wait:
				tcb->flags |= FORCE;
				if (tcb->timer.state != TcptimerON)
					tcpgo(tpriv, &tcb->timer);
		}

		if ((seg.flags & URG) && seg.urg) {
			if (seq_gt(seg.urg + seg.seq, tcb->rcv.urg)) {
				tcb->rcv.urg = seg.urg + seg.seq;
				pullblock(&bp, seg.urg);
			}
		} else if (seq_gt(tcb->rcv.nxt, tcb->rcv.urg))
			tcb->rcv.urg = tcb->rcv.nxt;

		if (length == 0) {
			if (bp != NULL)
				freeblist(bp);
		} else {
			switch (tcb->state) {
				default:
					/* Ignore segment text */
					if (bp != NULL)
						freeblist(bp);
					break;

				case Established:
				case Finwait1:
					/* If we still have some data place on
					 * receive queue
					 */
					if (bp) {
						bp = packblock(bp);
						if (bp == NULL)
							panic("tcp packblock");
						qpassnolim(s->rq, bp);
						bp = NULL;

						/*
						 *  Force an ack every 2 data messages.  This is
						 *  a hack for rob to make his home system run
						 *  faster.
						 *
						 *  this also keeps the standard TCP congestion
						 *  control working since it needs an ack every
						 *  2 max segs worth.  This is not quite that,
						 *  but under a real stream is equivalent since
						 *  every packet has a max seg in it.
						 */
						if (++(tcb->rcv.una) >= 2)
							tcb->flags |= FORCE;
					}
					tcb->rcv.nxt += length;
					drop_old_rcv_sacks(tcb);

					/*
					 *  update our rcv window
					 */
					tcprcvwin(s);

					/*
					 *  turn on the acktimer if there's something
					 *  to ack
					 */
					if (tcb->acktimer.state != TcptimerON)
						tcpgo(tpriv, &tcb->acktimer);

					break;
				case Finwait2:
					/* no process to read the data, send a reset */
					if (bp != NULL)
						freeblist(bp);
					sndrst(tcp, source, dest, length, &seg, version,
						   "send to Finwait2");
					qunlock(&s->qlock);
					poperror();
					return;
			}
		}

		if (seg.flags & FIN) {
			tcb->flags |= FORCE;

			switch (tcb->state) {
				case Established:
					tcb->rcv.nxt++;
					tcpsetstate(s, Close_wait);
					break;
				case Finwait1:
					tcb->rcv.nxt++;
					if (qlen(s->wq) + tcb->flgcnt == 0) {
						tcphalt(tpriv, &tcb->rtt_timer);
						tcphalt(tpriv, &tcb->acktimer);
						tcphalt(tpriv, &tcb->katimer);
						tcpsetstate(s, Time_wait);
						tcb->timer.start = MSL2 * (1000 / MSPTICK);
						tcpgo(tpriv, &tcb->timer);
					} else
						tcpsetstate(s, Closing);
					break;
				case Finwait2:
					tcb->rcv.nxt++;
					tcphalt(tpriv, &tcb->rtt_timer);
					tcphalt(tpriv, &tcb->acktimer);
					tcphalt(tpriv, &tcb->katimer);
					tcpsetstate(s, Time_wait);
					tcb->timer.start = MSL2 * (1000 / MSPTICK);
					tcpgo(tpriv, &tcb->timer);
					break;
				case Close_wait:
				case Closing:
				case Last_ack:
					break;
				case Time_wait:
					tcpgo(tpriv, &tcb->timer);
					break;
			}
		}

		/*
		 *  get next adjacent segment from the resequence queue.
		 *  dump/trim any overlapping segments
		 */
		for (;;) {
			if (tcb->reseq == NULL)
				goto output;

			if (seq_ge(tcb->rcv.nxt, tcb->reseq->seg.seq) == 0)
				goto output;

			getreseq(tcb, &seg, &bp, &length);

			if (tcptrim(tcb, &seg, &bp, &length) == 0)
				break;
		}
	}
output:
	tcpoutput(s);
	qunlock(&s->qlock);
	poperror();
	return;
raise:
	qunlock(&s->qlock);
	poperror();
	freeblist(bp);
	tcpkick(s);
}

/* The advertised mss = data + TCP headers */
static uint16_t derive_payload_mss(Tcpctl *tcb)
{
	uint16_t payload_mss = tcb->mss;
	uint16_t opt_size = 0;

	if (tcb->ts_recent) {
		opt_size += TS_LENGTH;
		/* Note that when we're a SYN, we overestimate slightly.  This is safe,
		 * and not really a problem. */
		opt_size += TS_SEND_PREPAD;
	}
	if (tcb->rcv.nr_sacks)
		opt_size += 2 + tcb->rcv.nr_sacks * 8;
	opt_size = ROUNDUP(opt_size, 4);
	payload_mss -= opt_size;
	return payload_mss;
}

/* Decreases the xmit amt, given the MSS / TSO. */
static uint32_t throttle_for_mss(Tcpctl *tcb, uint32_t ssize,
                                 uint16_t payload_mss, bool retrans)
{
	if (ssize > payload_mss) {
		if ((tcb->flags & TSO) == 0) {
			ssize = payload_mss;
		} else {
			/* Don't send too much.  32K is arbitrary.. */
			if (ssize > 32 * 1024)
				ssize = 32 * 1024;
			if (!retrans) {
				/* Clamp xmit to an integral MSS to avoid ragged tail segments
				 * causing poor link utilization. */
				ssize = ROUNDDOWN(ssize, payload_mss);
			}
		}
	}
	return ssize;
}

/* Reduces ssize for a variety of reasons.  Returns FALSE if we should abort
 * sending the packet.  o/w returns TRUE and modifies ssize by reference. */
static bool throttle_ssize(struct conv *s, Tcpctl *tcb, uint32_t *ssize_p,
                           uint16_t payload_mss, bool retrans)
{
	struct Fs *f = s->p->f;
	uint32_t usable;
	uint32_t ssize = *ssize_p;

	/* Compute usable segment based on offered window and limit
	 * window probes to one */
	if (tcb->snd.wnd == 0) {
		if (tcb->snd.in_flight != 0) {
			if ((tcb->flags & FORCE) == 0)
				return FALSE;
		}
		usable = 1;
	} else {
		usable = tcb->cwind;
		if (tcb->snd.wnd < usable)
			usable = tcb->snd.wnd;
		if (usable > tcb->snd.in_flight)
			usable -= tcb->snd.in_flight;
		else
			usable = 0;
		/* Avoid Silly Window Syndrome.  This is a little different thant RFC
		 * 813.  I took their additional enhancement of "< MSS" as an AND, not
		 * an OR.  25% of a large snd.wnd is pretty large, and our main goal is
		 * to avoid packets smaller than MSS.  I still use the 25% threshold,
		 * because it is important that there is *some* data in_flight.  If
		 * usable < MSS because snd.wnd is very small (but not 0), we might
		 * never get an ACK and would need to set up a timer.
		 *
		 * Also, I'm using 'ssize' as a proxy for a PSH point.  If there's just
		 * a small blob in the qio (or retrans!), then we might as well just
		 * send it. */
		if ((usable < tcb->typical_mss) && (usable < tcb->snd.wnd >> 2)
		    && (usable < ssize)) {
			return FALSE;
		}
	}
	if (ssize && usable < 2)
		netlog(s->p->f, Logtcpverbose,
		       "%I.%d -> %I.%d: throttled snd.wnd %lu cwind %lu\n",
		       s->laddr, s->lport, s->raddr, s->rport,
		       tcb->snd.wnd, tcb->cwind);
	if (usable < ssize)
		ssize = usable;

	ssize = throttle_for_mss(tcb, ssize, payload_mss, retrans);

	*ssize_p = ssize;
	return TRUE;
}

/* Helper, picks the next segment to send, which is possibly a retransmission.
 * Returns TRUE if we have a segment, FALSE o/w.  Returns ssize, from_seq, and
 * sent by reference.
 *
 * from_seq is the seq number we are transmitting from.
 *
 * sent includes all seq from una to from_seq *including* any previously sent
 * flags (part of tcb->flgcnt), for instance an unacknowledged SYN (which counts
 * as a seq number).  Those flags are in the e.g. snd.nxt - snd.una range, and
 * they get dropped after qdiscard.
 *
 * ssize is the amount of data we are sending, starting from from_seq, and it
 * will include any *new* flags, which haven't been accounted for yet.
 *
 * tcb->flgcnt consists of the flags both in ssize and in sent.
 *
 * Note that we could be in recovery and not sack_retrans a segment. */
static bool get_xmit_segment(struct conv *s, Tcpctl *tcb, uint16_t payload_mss,
                             uint32_t *from_seq_p, uint32_t *sent_p,
                             uint32_t *ssize_p)
{
	struct Fs *f = s->p->f;
	struct tcppriv *tpriv = s->p->priv;
	uint32_t ssize, sent, from_seq;
	bool sack_retrans = FALSE;
	struct sack_block *tcb_sack = 0;

	for (int i = 0; i < tcb->snd.nr_sacks; i++) {
		tcb_sack = &tcb->snd.sacks[i];
		if (seq_lt(tcb->snd.rtx, tcb_sack->left)) {
			/* So ssize is supposed to include any *new* flags to flgcnt, which
			 * at this point would be a FIN.
			 *
			 * It might be possible that flgcnt is incremented so we send a FIN,
			 * even for an intermediate sack retrans.  Perhaps the user closed
			 * the conv.
			 *
			 * However, the way the "flgcnt for FIN" works is that it inflates
			 * the desired amount we'd like to send (qlen + flgcnt).
			 * Eventually, we reach the end of the queue and fail to extract all
			 * of dsize.  At that point, we put on the FIN, and that's where the
			 * extra 'byte' comes from.
			 *
			 * For sack retrans, since we're extracting from parts of the qio
			 * that aren't the right-most edge, we don't need to consider flgcnt
			 * when setting ssize. */
			from_seq = tcb->snd.rtx;
			sent = from_seq - tcb->snd.una;
			ssize = tcb_sack->left - from_seq;
			sack_retrans = TRUE;
			break;
		}
	}
	/* SACK holes have first dibs, but we can still opportunisitically send new
	 * data.
	 *
	 * During other types of recovery, we'll just send from the retrans point.
	 * If we're in an RTO while we still have sacks, we could be resending data
	 * that wasn't lost.  Consider a sack that is still growing (usually the
	 * right-most), but we haven't received the ACK yet.  rxt may be included in
	 * that area.  Given we had two losses or otherwise timed out, I'm not too
	 * concerned.
	 *
	 * Note that Fast and RTO can send data beyond nxt.  If we change that,
	 * change the accounting below. */
	if (!sack_retrans) {
		switch (tcb->snd.recovery) {
		default:
		case SACK_RETRANS_RECOVERY:
			from_seq = tcb->snd.nxt;
			break;
		case FAST_RETRANS_RECOVERY:
		case RTO_RETRANS_RECOVERY:
			from_seq = tcb->snd.rtx;
			break;
		}
		sent = from_seq - tcb->snd.una;
		/* qlen + flgcnt is every seq we want to have sent, including unack'd
		 * data, unacked flags, and new flags. */
		ssize = qlen(s->wq) + tcb->flgcnt - sent;
	}

	if (!throttle_ssize(s, tcb, &ssize, payload_mss, sack_retrans))
		return FALSE;

	/* This counts flags, which is a little hokey, but it's okay since in_flight
	 * gets reset on each ACK */
	tcb->snd.in_flight += ssize;
	/* Log and track rxmit.  This covers both SACK (retrans) and fast rxmit. */
	if (ssize && seq_lt(tcb->snd.rtx, tcb->snd.nxt)) {
		netlog(f, Logtcpverbose,
		       "%I.%d -> %I.%d: rxmit: rtx %u amt %u, nxt %u\n",
		       s->laddr, s->lport, s->raddr, s->rport,
		       tcb->snd.rtx, MIN(tcb->snd.nxt - tcb->snd.rtx, ssize),
		       tcb->snd.nxt);
		tpriv->stats[RetransSegs]++;
	}
	if (sack_retrans) {
		/* If we'll send up to the left edge, advance snd.rtx to the right.
		 *
		 * This includes the largest sack.  It might get removed later, in which
		 * case we'll underestimate the amount in-flight.  The alternative is to
		 * not count the rightmost sack, but when it gets removed, we'll retrans
		 * it anyway.  No matter what, we'd count it. */
		tcb->snd.rtx += ssize;
		if (tcb->snd.rtx == tcb_sack->left)
			tcb->snd.rtx = tcb_sack->right;
		/* RFC 6675 says we MAY rearm the RTO timer on each retrans, since we
		 * might not be getting ACKs for a while. */
		tcpsettimer(tcb);
	} else {
		switch (tcb->snd.recovery) {
		default:
			/* under normal op, we drag rtx along with nxt.  this prevents us
			 * from sending sacks too early (up above), since rtx doesn't get
			 * reset to una until we have a loss (e.g. 3 dupacks/sacks). */
			tcb->snd.nxt += ssize;
			tcb->snd.rtx = tcb->snd.nxt;
			break;
		case SACK_RETRANS_RECOVERY:
			/* We explicitly do not want to increase rtx here.  We might still
			 * need it to fill in a sack gap below nxt if we get new, higher
			 * sacks. */
			tcb->snd.nxt += ssize;
			break;
		case FAST_RETRANS_RECOVERY:
		case RTO_RETRANS_RECOVERY:
			tcb->snd.rtx += ssize;
			/* Fast and RTO can send new data, advancing nxt. */
			if (seq_gt(tcb->snd.rtx, tcb->snd.nxt))
				tcb->snd.nxt = tcb->snd.rtx;
			break;
		}
	}
	*from_seq_p = from_seq;
	*sent_p = sent;
	*ssize_p = ssize;

	return TRUE;
}

/*
 *  always enters and exits with the s locked.  We drop
 *  the lock to ipoput the packet so some care has to be
 *  taken by callers.
 */
static void tcpoutput(struct conv *s)
{
	Tcp seg;
	int msgs;
	int next_yield = 1;
	Tcpctl *tcb;
	struct block *hbp, *bp;
	uint32_t ssize, dsize, sent, from_seq;
	struct Fs *f;
	struct tcppriv *tpriv;
	uint8_t version;
	uint16_t payload_mss;

	f = s->p->f;
	tpriv = s->p->priv;
	version = s->ipversion;

	for (msgs = 0; msgs < 100; msgs++) {
		tcb = (Tcpctl *) s->ptcl;

		switch (tcb->state) {
			case Listen:
			case Closed:
			case Finwait2:
				return;
		}

		/* force an ack when a window has opened up */
		if (tcb->rcv.blocked && tcb->rcv.wnd >= tcb->mss) {
			tcb->rcv.blocked = 0;
			tcb->flags |= FORCE;
		}

		/* Don't send anything else until our SYN has been acked */
		if (tcb->snd.nxt != tcb->iss && (tcb->flags & SYNACK) == 0)
			break;

		/* payload_mss is the actual amount of data in the packet, which is the
		 * advertised (mss - header opts).  This varies from packet to packet,
		 * based on the options that might be present (e.g. always timestamps,
		 * sometimes SACKs) */
		payload_mss = derive_payload_mss(tcb);

		if (!get_xmit_segment(s, tcb, payload_mss, &from_seq, &sent, &ssize))
			break;

		dsize = ssize;
		seg.urg = 0;

		if (ssize == 0)
			if ((tcb->flags & FORCE) == 0)
				break;

		tcb->flags &= ~FORCE;
		tcprcvwin(s);

		/* By default we will generate an ack, so we can normally turn off the
		 * timer.  If we're blocked, we'll want the timer so we can send a
		 * window update. */
		if (!tcb->rcv.blocked)
			tcphalt(tpriv, &tcb->acktimer);
		tcb->rcv.una = 0;
		seg.source = s->lport;
		seg.dest = s->rport;
		seg.flags = ACK;
		seg.mss = 0;
		seg.ws = 0;
		seg.sack_ok = FALSE;
		seg.nr_sacks = 0;
		/* When outputting, Syn_sent means "send the Syn", for connections we
		 * initiate.  SYNACKs are sent from sndsynack directly. */
		if (tcb->state == Syn_sent) {
			seg.flags = 0;
			seg.sack_ok = SACK_SUPPORTED;	/* here's where we advertise SACK */
			if (tcb->snd.nxt - ssize == tcb->iss) {
				seg.flags |= SYN;
				dsize--;
				seg.mss = tcb->mss;
				seg.ws = tcb->scale;
			} else {
				/* TODO: Not sure why we'd get here. */
				warn("TCP: weird Syn_sent state, tell someone you saw this");
			}
		}
		seg.seq = from_seq;
		seg.ack = tcb->rcv.nxt;
		tcb->last_ack_sent = seg.ack;
		seg.wnd = tcb->rcv.wnd;
		seg.ts_val = tcb->ts_recent;

		/* Pull out data to send */
		bp = NULL;
		if (dsize != 0) {
			bp = qcopy(s->wq, dsize, sent);
			if (BLEN(bp) != dsize) {
				/* Here's where the flgcnt kicked in.  Note dsize is
				 * decremented, but ssize isn't.  Not that we use ssize for much
				 * anymore.  Decrementing dsize prevents us from sending a PSH
				 * with the FIN. */
				seg.flags |= FIN;
				dsize--;
			}
			if (BLEN(bp) > payload_mss) {
				bp->flag |= Btso;
				bp->mss = payload_mss;
			}
		}

		if (sent + dsize == qlen(s->wq) + tcb->flgcnt)
			seg.flags |= PSH;

		/* Build header, link data and compute cksum */
		switch (version) {
			case V4:
				tcb->protohdr.tcp4hdr.vihl = IP_VER4;
				hbp = htontcp4(&seg, bp, &tcb->protohdr.tcp4hdr, tcb);
				if (hbp == NULL) {
					freeblist(bp);
					return;
				}
				break;
			case V6:
				tcb->protohdr.tcp6hdr.vcf[0] = IP_VER6;
				hbp = htontcp6(&seg, bp, &tcb->protohdr.tcp6hdr, tcb);
				if (hbp == NULL) {
					freeblist(bp);
					return;
				}
				break;
			default:
				hbp = NULL;	/* to suppress a warning */
				panic("tcpoutput: version %d", version);
		}

		/* Start the transmission timers if there is new data and we
		 * expect acknowledges
		 */
		if (ssize != 0) {
			if (tcb->timer.state != TcptimerON)
				tcpgo(tpriv, &tcb->timer);

			if (!tcb->ts_recent && (tcb->rtt_timer.state != TcptimerON)) {
				/* If round trip timer isn't running, start it. */
				tcpgo(tpriv, &tcb->rtt_timer);
				tcb->rttseq = from_seq + ssize;
			}
		}

		tpriv->stats[OutSegs]++;

		/* put off the next keep alive */
		tcpgo(tpriv, &tcb->katimer);

		switch (version) {
			case V4:
				if (ipoput4(f, hbp, 0, s->ttl, s->tos, s) < 0) {
					/* a negative return means no route */
					localclose(s, "no route");
				}
				break;
			case V6:
				if (ipoput6(f, hbp, 0, s->ttl, s->tos, s) < 0) {
					/* a negative return means no route */
					localclose(s, "no route");
				}
				break;
			default:
				panic("tcpoutput2: version %d", version);
		}
		if (ssize) {
			/* The outer loop thinks we sent one packet.  If we used TSO, we
			 * might have sent several.  Minus one for the loop increment. */
			msgs += DIV_ROUND_UP(ssize, payload_mss) - 1;
		}
		/* Old Plan 9 tidbit - yield every four messages.  We want to break out
		 * and unlock so we can process inbound ACKs which might do things like
		 * say "slow down". */
		if (msgs >= next_yield) {
			next_yield = msgs + 4;
			qunlock(&s->qlock);
			kthread_yield();
			qlock(&s->qlock);
		}
	}
}

/*
 *  the BSD convention (hack?) for keep alives.  resend last uint8_t acked.
 */
static void tcpsendka(struct conv *s)
{
	Tcp seg;
	Tcpctl *tcb;
	struct block *hbp, *dbp;

	tcb = (Tcpctl *) s->ptcl;

	dbp = NULL;
	seg.urg = 0;
	seg.source = s->lport;
	seg.dest = s->rport;
	seg.flags = ACK | PSH;
	seg.mss = 0;
	seg.ws = 0;
	seg.sack_ok = FALSE;
	seg.nr_sacks = 0;
	if (tcpporthogdefense)
		urandom_read(&seg.seq, sizeof(seg.seq));
	else
		seg.seq = tcb->snd.una - 1;
	seg.ack = tcb->rcv.nxt;
	tcb->last_ack_sent = seg.ack;
	tcb->rcv.una = 0;
	seg.wnd = tcb->rcv.wnd;
	seg.ts_val = tcb->ts_recent;
	if (tcb->state == Finwait2) {
		seg.flags |= FIN;
	} else {
		dbp = block_alloc(1, MEM_WAIT);
		dbp->wp++;
	}

	if (isv4(s->raddr)) {
		/* Build header, link data and compute cksum */
		tcb->protohdr.tcp4hdr.vihl = IP_VER4;
		hbp = htontcp4(&seg, dbp, &tcb->protohdr.tcp4hdr, tcb);
		if (hbp == NULL) {
			freeblist(dbp);
			return;
		}
		ipoput4(s->p->f, hbp, 0, s->ttl, s->tos, s);
	} else {
		/* Build header, link data and compute cksum */
		tcb->protohdr.tcp6hdr.vcf[0] = IP_VER6;
		hbp = htontcp6(&seg, dbp, &tcb->protohdr.tcp6hdr, tcb);
		if (hbp == NULL) {
			freeblist(dbp);
			return;
		}
		ipoput6(s->p->f, hbp, 0, s->ttl, s->tos, s);
	}
}

/*
 *  set connection to time out after 12 minutes
 */
static void tcpsetkacounter(Tcpctl *tcb)
{
	tcb->kacounter = (12 * 60 * 1000) / (tcb->katimer.start * MSPTICK);
	if (tcb->kacounter < 3)
		tcb->kacounter = 3;
}

/*
 *  if we've timed out, close the connection
 *  otherwise, send a keepalive and restart the timer
 */
static void tcpkeepalive(void *v)
{
	ERRSTACK(1);
	Tcpctl *tcb;
	struct conv *s;

	s = v;
	tcb = (Tcpctl *) s->ptcl;
	qlock(&s->qlock);
	if (waserror()) {
		qunlock(&s->qlock);
		nexterror();
	}
	if (tcb->state != Closed) {
		if (--(tcb->kacounter) <= 0) {
			localclose(s, "connection timed out");
		} else {
			tcpsendka(s);
			tcpgo(s->p->priv, &tcb->katimer);
		}
	}
	qunlock(&s->qlock);
	poperror();
}

/*
 *  start keepalive timer
 */
static void tcpstartka(struct conv *s, char **f, int n)
{
	Tcpctl *tcb;
	int x;

	tcb = (Tcpctl *) s->ptcl;
	if (tcb->state != Established)
		error(ENOTCONN, "connection must be in Establised state");
	if (n > 1) {
		x = atoi(f[1]);
		if (x >= MSPTICK)
			tcb->katimer.start = x / MSPTICK;
	}
	tcpsetkacounter(tcb);
	tcpgo(s->p->priv, &tcb->katimer);
}

/*
 *  turn checksums on/off
 */
static void tcpsetchecksum(struct conv *s, char **f, int unused)
{
	Tcpctl *tcb;

	tcb = (Tcpctl *) s->ptcl;
	tcb->nochecksum = !atoi(f[1]);
}

static void tcp_loss_event(struct conv *s, Tcpctl *tcb)
{
	uint32_t old_cwnd = tcb->cwind;

	/* Reno */
	tcb->ssthresh = tcb->cwind / 2;
	tcb->cwind = tcb->ssthresh;
	netlog(s->p->f, Logtcprxmt,
	       "%I.%d -> %I.%d: loss event, cwnd was %d, now %d\n",
	       s->laddr, s->lport, s->raddr, s->rport,
	       old_cwnd, tcb->cwind);
}

/* Called when we need to retrans the entire outstanding window (everything
 * previously sent, but unacknowledged). */
static void tcprxmit(struct conv *s)
{
	Tcpctl *tcb;

	tcb = (Tcpctl *) s->ptcl;

	tcb->flags |= FORCE;
	tcb->snd.rtx = tcb->snd.una;
	set_in_flight(tcb);

	tcpoutput(s);
}

/* The original RFC said to drop sacks on a timeout, since the receiver could
 * renege.  Later RFCs say we can keep them around, so long as we are careful.
 *
 * We'll go with a "flush if we have two timeouts" plan.  This doesn't have to
 * be perfect - there might be cases where we accidentally flush the sacks too
 * often.  Perhaps we never get dup_acks to start fast/sack rxmit.  The main
 * thing is that after multiple timeouts we flush the sacks, since the receiver
 * might renege.
 *
 * We also have an Akaros-specific problem.  We use the sacks to determine
 * in_flight.  Specifically, the (snd.nxt - upper right edge) is tracked as in
 * flight.  Usually the receiver will keep sacking that right edge all the way
 * up to snd.nxt, but they might not, and the gap might be quite large.  After a
 * timeout, that data is definitely not in flight.  If that block's size is
 * greater than cwnd, we'll never transmit.  This should be rare, and in that
 * case we can just dump the sacks.  The typical_mss fudge factor is so we can
 * send a reasonably-sized packet. */
static void timeout_handle_sacks(Tcpctl *tcb)
{
	struct sack_block *last_sack;

	if (tcb->snd.nr_sacks) {
		last_sack = &tcb->snd.sacks[tcb->snd.nr_sacks - 1];
		if (tcb->snd.flush_sacks || (tcb->snd.nxt - last_sack->right >=
		                             tcb->cwind - tcb->typical_mss)) {
			tcb->snd.nr_sacks = 0;
			tcb->snd.flush_sacks = FALSE;
		} else {
			tcb->snd.flush_sacks = TRUE;
		}
	}
}

static void tcptimeout(void *arg)
{
	ERRSTACK(1);
	struct conv *s;
	Tcpctl *tcb;
	int maxback;
	struct tcppriv *tpriv;

	s = (struct conv *)arg;
	tpriv = s->p->priv;
	tcb = (Tcpctl *) s->ptcl;

	qlock(&s->qlock);
	if (waserror()) {
		qunlock(&s->qlock);
		nexterror();
	}
	switch (tcb->state) {
		default:
			tcb->backoff++;
			if (tcb->state == Syn_sent)
				maxback = MAXBACKMS / 2;
			else
				maxback = MAXBACKMS;
			tcb->backedoff += tcb->timer.start * MSPTICK;
			if (tcb->backedoff >= maxback) {
				localclose(s, "connection timed out");
				break;
			}
			netlog(s->p->f, Logtcprxmt,
			       "%I.%d -> %I.%d: timeout rxmit una %u, rtx %u, nxt %u, in_flight %u, timer.start %u\n",
			       s->laddr, s->lport, s->raddr, s->rport,
			       tcb->snd.una, tcb->snd.rtx, tcb->snd.nxt, tcb->snd.in_flight,
			       tcb->timer.start);
			tcpsettimer(tcb);
			tcp_loss_event(s, tcb);
			/* Advance the recovery point.  Any dupacks/sacks below this won't
			 * trigger a new loss, since we won't reset_recovery() until we ack
			 * past recovery_pt. */
			tcb->snd.recovery = RTO_RETRANS_RECOVERY;
			tcb->snd.recovery_pt = tcb->snd.nxt;
			timeout_handle_sacks(tcb);
			tcprxmit(s);
			tpriv->stats[RetransTimeouts]++;
			break;
		case Time_wait:
			localclose(s, NULL);
			break;
		case Closed:
			break;
	}
	qunlock(&s->qlock);
	poperror();
}

static int inwindow(Tcpctl *tcb, int seq)
{
	return seq_within(seq, tcb->rcv.nxt, tcb->rcv.nxt + tcb->rcv.wnd - 1);
}

/*
 *  set up state for a received SYN (or SYN ACK) packet
 */
static void procsyn(struct conv *s, Tcp *seg)
{
	Tcpctl *tcb;

	tcb = (Tcpctl *) s->ptcl;
	tcb->flags |= FORCE;

	tcb->rcv.nxt = seg->seq + 1;
	tcb->rcv.urg = tcb->rcv.nxt;
	tcb->irs = seg->seq;

	/* our sending max segment size cannot be bigger than what he asked for */
	if (seg->mss != 0 && seg->mss < tcb->mss) {
		tcb->mss = seg->mss;
		tcb->typical_mss = tcb->mss;
	}
	adjust_typical_mss_for_opts(seg, tcb);

	tcb->snd.wnd = seg->wnd;
	tcb->cwind = tcb->typical_mss * CWIND_SCALE;
}

static int addreseq(Tcpctl *tcb, struct tcppriv *tpriv, Tcp *seg,
                    struct block *bp, uint16_t length)
{
	Reseq *rp, *rp1;
	int i, rqlen, qmax;

	rp = kzmalloc(sizeof(Reseq), 0);
	if (rp == NULL) {
		freeblist(bp);	/* bp always consumed by add_reseq */
		return 0;
	}

	rp->seg = *seg;
	rp->bp = bp;
	rp->length = length;

	track_rcv_sack(tcb, seg->seq, seg->seq + length);
	/* Place on reassembly list sorting by starting seq number */
	rp1 = tcb->reseq;
	if (rp1 == NULL || seq_lt(seg->seq, rp1->seg.seq)) {
		rp->next = rp1;
		tcb->reseq = rp;
		if (rp->next != NULL)
			tpriv->stats[OutOfOrder]++;
		return 0;
	}

	rqlen = 0;
	for (i = 0;; i++) {
		rqlen += rp1->length;
		if (rp1->next == NULL || seq_lt(seg->seq, rp1->next->seg.seq)) {
			rp->next = rp1->next;
			rp1->next = rp;
			if (rp->next != NULL)
				tpriv->stats[OutOfOrder]++;
			break;
		}
		rp1 = rp1->next;
	}
	qmax = QMAX << tcb->rcv.scale;
	/* Here's where we're reneging on previously reported sacks. */
	if (rqlen > qmax) {
		printd("resequence queue > window: %d > %d\n", rqlen, qmax);
		i = 0;
		for (rp1 = tcb->reseq; rp1 != NULL; rp1 = rp1->next) {
			printd("0x%#lx 0x%#lx 0x%#x\n", rp1->seg.seq,
				   rp1->seg.ack, rp1->seg.flags);
			if (i++ > 10) {
				printd("...\n");
				break;
			}
		}

		// delete entire reassembly queue; wait for retransmit.
		// - should we be smarter and only delete the tail?
		for (rp = tcb->reseq; rp != NULL; rp = rp1) {
			rp1 = rp->next;
			freeblist(rp->bp);
			kfree(rp);
		}
		tcb->reseq = NULL;
		tcb->rcv.nr_sacks = 0;

		return -1;
	}
	return 0;
}

static void getreseq(Tcpctl *tcb, Tcp *seg, struct block **bp, uint16_t *length)
{
	Reseq *rp;

	rp = tcb->reseq;
	if (rp == NULL)
		return;

	tcb->reseq = rp->next;

	*seg = rp->seg;
	*bp = rp->bp;
	*length = rp->length;

	kfree(rp);
}

static int tcptrim(Tcpctl *tcb, Tcp *seg, struct block **bp, uint16_t *length)
{
	uint16_t len;
	uint8_t accept;
	int dupcnt, excess;

	accept = 0;
	len = *length;
	if (seg->flags & SYN)
		len++;
	if (seg->flags & FIN)
		len++;

	if (tcb->rcv.wnd == 0) {
		if (len == 0 && seg->seq == tcb->rcv.nxt)
			return 0;
	} else {
		/* Some part of the segment should be in the window */
		if (inwindow(tcb, seg->seq))
			accept++;
		else if (len != 0) {
			if (inwindow(tcb, seg->seq + len - 1) ||
				seq_within(tcb->rcv.nxt, seg->seq, seg->seq + len - 1))
				accept++;
		}
	}
	if (!accept) {
		freeblist(*bp);
		return -1;
	}
	dupcnt = tcb->rcv.nxt - seg->seq;
	if (dupcnt > 0) {
		tcb->rerecv += dupcnt;
		if (seg->flags & SYN) {
			seg->flags &= ~SYN;
			seg->seq++;

			if (seg->urg > 1)
				seg->urg--;
			else
				seg->flags &= ~URG;
			dupcnt--;
		}
		if (dupcnt > 0) {
			pullblock(bp, (uint16_t) dupcnt);
			seg->seq += dupcnt;
			*length -= dupcnt;

			if (seg->urg > dupcnt)
				seg->urg -= dupcnt;
			else {
				seg->flags &= ~URG;
				seg->urg = 0;
			}
		}
	}
	excess = seg->seq + *length - (tcb->rcv.nxt + tcb->rcv.wnd);
	if (excess > 0) {
		tcb->rerecv += excess;
		*length -= excess;
		*bp = trimblock(*bp, 0, *length);
		if (*bp == NULL)
			panic("presotto is a boofhead");
		seg->flags &= ~FIN;
	}
	return 0;
}

static void tcpadvise(struct Proto *tcp, struct block *bp, char *msg)
{
	Tcp4hdr *h4;
	Tcp6hdr *h6;
	Tcpctl *tcb;
	uint8_t source[IPaddrlen];
	uint8_t dest[IPaddrlen];
	uint16_t psource, pdest;
	struct conv *s, **p;

	h4 = (Tcp4hdr *) (bp->rp);
	h6 = (Tcp6hdr *) (bp->rp);

	if ((h4->vihl & 0xF0) == IP_VER4) {
		v4tov6(dest, h4->tcpdst);
		v4tov6(source, h4->tcpsrc);
		psource = nhgets(h4->tcpsport);
		pdest = nhgets(h4->tcpdport);
	} else {
		ipmove(dest, h6->tcpdst);
		ipmove(source, h6->tcpsrc);
		psource = nhgets(h6->tcpsport);
		pdest = nhgets(h6->tcpdport);
	}

	/* Look for a connection */
	for (p = tcp->conv; *p; p++) {
		s = *p;
		tcb = (Tcpctl *) s->ptcl;
		if (s->rport == pdest)
			if (s->lport == psource)
				if (tcb->state != Closed)
					if (ipcmp(s->raddr, dest) == 0)
						if (ipcmp(s->laddr, source) == 0) {
							qlock(&s->qlock);
							switch (tcb->state) {
								case Syn_sent:
									localclose(s, msg);
									break;
							}
							qunlock(&s->qlock);
							freeblist(bp);
							return;
						}
	}
	freeblist(bp);
}

static void tcpporthogdefensectl(char *val)
{
	if (strcmp(val, "on") == 0)
		tcpporthogdefense = 1;
	else if (strcmp(val, "off") == 0)
		tcpporthogdefense = 0;
	else
		error(EINVAL, "unknown value for tcpporthogdefense");
}

/* called with c qlocked */
static void tcpctl(struct conv *c, char **f, int n)
{
	if (n == 1 && strcmp(f[0], "hangup") == 0)
		tcphangup(c);
	else if (n >= 1 && strcmp(f[0], "keepalive") == 0)
		tcpstartka(c, f, n);
	else if (n >= 1 && strcmp(f[0], "checksum") == 0)
		tcpsetchecksum(c, f, n);
	else if (n >= 1 && strcmp(f[0], "tcpporthogdefense") == 0)
		tcpporthogdefensectl(f[1]);
	else
		error(EINVAL, "unknown command to %s", __func__);
}

static int tcpstats(struct Proto *tcp, char *buf, int len)
{
	struct tcppriv *priv;
	char *p, *e;
	int i;

	priv = tcp->priv;
	p = buf;
	e = p + len;
	for (i = 0; i < Nstats; i++)
		p = seprintf(p, e, "%s: %u\n", statnames[i], priv->stats[i]);
	return p - buf;
}

/*
 *  garbage collect any stale conversations:
 *	- SYN received but no SYN-ACK after 5 seconds (could be the SYN attack)
 *	- Finwait2 after 5 minutes
 *
 *  this is called whenever we run out of channels.  Both checks are
 *  of questionable validity so we try to use them only when we're
 *  up against the wall.
 */
static int tcpgc(struct Proto *tcp)
{
	struct conv *c, **pp, **ep;
	int n;
	Tcpctl *tcb;

	n = 0;
	ep = &tcp->conv[tcp->nc];
	for (pp = tcp->conv; pp < ep; pp++) {
		c = *pp;
		if (c == NULL)
			break;
		if (!canqlock(&c->qlock))
			continue;
		tcb = (Tcpctl *) c->ptcl;
		if (tcb->state == Finwait2) {
			if (NOW - tcb->time > 5 * 60 * 1000) {
				localclose(c, "timed out");
				n++;
			}
		}
		qunlock(&c->qlock);
	}
	return n;
}

static void tcpsettimer(Tcpctl *tcb)
{
	int x;

	/* round trip dependency */
	x = backoff(tcb->backoff) * (tcb->srtt + MAX(4 * tcb->mdev, MSPTICK));
	x = DIV_ROUND_UP(x, MSPTICK);

	/* Bounded twixt 1/2 and 64 seconds.  RFC 6298 suggested min is 1 second. */
	if (x < 500 / MSPTICK)
		x = 500 / MSPTICK;
	else if (x > (64000 / MSPTICK))
		x = 64000 / MSPTICK;
	tcb->timer.start = x;
}

static struct tcppriv *debug_priv;

/* Kfunc this */
int dump_tcp_ht(void)
{
	if (!debug_priv)
		return -1;
	dump_ipht(&debug_priv->ht);
	return 0;
}

void tcpinit(struct Fs *fs)
{
	struct Proto *tcp;
	struct tcppriv *tpriv;

	tcp = kzmalloc(sizeof(struct Proto), 0);
	tpriv = tcp->priv = kzmalloc(sizeof(struct tcppriv), 0);
	debug_priv = tpriv;
	qlock_init(&tpriv->tl);
	qlock_init(&tpriv->apl);
	tcp->name = "tcp";
	tcp->connect = tcpconnect;
	tcp->announce = tcpannounce;
	tcp->bypass = tcpbypass;
	tcp->ctl = tcpctl;
	tcp->state = tcpstate;
	tcp->create = tcpcreate;
	tcp->close = tcpclose;
	tcp->shutdown = tcpshutdown;
	tcp->rcv = tcpiput;
	tcp->advise = tcpadvise;
	tcp->stats = tcpstats;
	tcp->inuse = tcpinuse;
	tcp->gc = tcpgc;
	tcp->ipproto = IP_TCPPROTO;
	tcp->nc = 4096;
	tcp->ptclsize = sizeof(Tcpctl);
	tpriv->stats[MaxConn] = tcp->nc;

	Fsproto(fs, tcp);
}

static void tcpsetscale(struct conv *s, Tcpctl *tcb, uint16_t rcvscale,
                        uint16_t sndscale)
{
	if (rcvscale) {
		tcb->rcv.scale = rcvscale & 0xff;
		tcb->snd.scale = sndscale & 0xff;
		tcb->window = QMAX << tcb->rcv.scale;
	} else {
		tcb->rcv.scale = 0;
		tcb->snd.scale = 0;
		tcb->window = QMAX;
	}
}
