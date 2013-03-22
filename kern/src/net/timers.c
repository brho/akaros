/**
 * @file
 * Stack-internal timers implementation.
 * This file includes timer callbacks for stack-internal timers as well as
 * functions to set up or stop timers and check for expired timers.
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *         Simon Goldschmidt
 *
 */
#include "smp.h"
#include "net/timers.h"
#include "net/tcp_impl.h"
#include "net/tcp.h"

/** The one and only timeout list */
static struct sys_timeo *next_timeout;

/** global variable that shows if the tcp timer is currently scheduled or not */
static bool tcpip_tcp_timer_active = false;  // default to not active
static struct alarm_waiter *tcp_waiter = NULL; // initialized to empty waiter

/**
 * Timer callback function that calls tcp_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void tcpip_tcp_timer(struct alarm_waiter *aw) {
  /* call TCP timer handler */
  tcp_tmr();
	if (aw == NULL) {
		// need to allocate a fresh waiter
	}
  /* timer still needed? */
  if (tcp_active_pcbs || tcp_tw_pcbs) {
 		struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
		set_awaiter_rel(aw, TCP_TMR_INTERVAL);
		set_alarm(tchain, aw);
		tcpip_tcp_timer_active = true;
  } else {
    /* disable timer */
    tcpip_tcp_timer_active = false;
  }
}

/**
 * Called from TCP_REG when registering a new PCB:
 * the reason is to have the TCP timer only running when
 * there are active (or time-wait) PCBs.
 */
void tcp_timer_needed(void) {
  /* timer is off but needed again? */
  if (!tcpip_tcp_timer_active && (tcp_active_pcbs || tcp_tw_pcbs)) {
    /* enable and start timer */
    tcpip_tcp_timer_active = true;
		tcp_waiter = sys_timeout(TCP_TMR_INTERVAL, tcpip_tcp_timer, tcp_waiter);
	} else {
		if (tcp_waiter != NULL) {
 			struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
			unset_alarm(tchain, tcp_waiter);
		}
	}
}

#if IP_REASSEMBLY
/**
 * Timer callback function that calls ip_reass_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
ip_reass_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: ip_reass_tmr()\n"));
  ip_reass_tmr();
  sys_timeout(IP_TMR_INTERVAL, ip_reass_timer, NULL);
}
#endif /* IP_REASSEMBLY */

#if LWIP_ARP
/**
 * Timer callback function that calls etharp_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
arp_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: etharp_tmr()\n"));
  etharp_tmr();
  sys_timeout(ARP_TMR_INTERVAL, arp_timer, NULL);
}
#endif /* LWIP_ARP */

#if LWIP_DHCP
/**
 * Timer callback function that calls dhcp_coarse_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
dhcp_timer_coarse(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: dhcp_coarse_tmr()\n"));
  dhcp_coarse_tmr();
  sys_timeout(DHCP_COARSE_TIMER_MSECS, dhcp_timer_coarse, NULL);
}

/**
 * Timer callback function that calls dhcp_fine_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
dhcp_timer_fine(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: dhcp_fine_tmr()\n"));
  dhcp_fine_tmr();
  sys_timeout(DHCP_FINE_TIMER_MSECS, dhcp_timer_fine, NULL);
}
#endif /* LWIP_DHCP */

#if LWIP_AUTOIP
/**
 * Timer callback function that calls autoip_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
autoip_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: autoip_tmr()\n"));
  autoip_tmr();
  sys_timeout(AUTOIP_TMR_INTERVAL, autoip_timer, NULL);
}
#endif /* LWIP_AUTOIP */

#if LWIP_IGMP
/**
 * Timer callback function that calls igmp_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
igmp_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: igmp_tmr()\n"));
  igmp_tmr();
  sys_timeout(IGMP_TMR_INTERVAL, igmp_timer, NULL);
}
#endif /* LWIP_IGMP */

#if LWIP_DNS
/**
 * Timer callback function that calls dns_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
dns_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: dns_tmr()\n"));
  dns_tmr();
  sys_timeout(DNS_TMR_INTERVAL, dns_timer, NULL);
}
#endif /* LWIP_DNS */

/** Initialize this module */
void sys_timeouts_init(void)
{

	//we don't support ipreassembly
  // ip_reass_waiter = sys_timeout(IP_TMR_INTERVAL, ip_reass_timer, ip_reass_waiter);
#if 0 
#if LWIP_ARP
  sys_timeout(ARP_TMR_INTERVAL, arp_timer, NULL);
#endif /* LWIP_ARP */
#if LWIP_DHCP
  sys_timeout(DHCP_COARSE_TIMER_MSECS, dhcp_timer_coarse, NULL);
  sys_timeout(DHCP_FINE_TIMER_MSECS, dhcp_timer_fine, NULL);
#endif /* LWIP_DHCP */
#if LWIP_AUTOIP
  sys_timeout(AUTOIP_TMR_INTERVAL, autoip_timer, NULL);
#endif /* LWIP_AUTOIP */
#if LWIP_IGMP
  sys_timeout(IGMP_TMR_INTERVAL, igmp_timer, NULL);
#endif /* LWIP_IGMP */
#if LWIP_DNS
  sys_timeout(DNS_TMR_INTERVAL, dns_timer, NULL);
#endif /* LWIP_DNS */
	*/
#endif
}

/**
 * Create a one-shot timer (aka timeout). Timeouts are processed in the
 * following cases:
 * @param msecs time in milliseconds after that the timer should expire
 * @param handler callback function to call when msecs have elapsed, this handler takes no arguments
 */
struct alarm_waiter* sys_timeout(uint32_t msecs, sys_timeout_handler func, struct alarm_waiter* waiter) {
	if (waiter == NULL) {
		waiter = kmalloc(sizeof(struct alarm_waiter), 0);
	}
 	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	// initialize the waiter
	init_awaiter(waiter, func);
	// explicitly setting the waiter data to be null, since we do not need it in our handler
	waiter->data = NULL;
	// set waiting time
	set_awaiter_rel(waiter, 1000 * msecs);
	// attach the waiter to a chain to wait
	set_alarm(tchain, waiter);

	// XXX: when to destroy the waiter, handlers responsibility?
	return waiter;
}

/**
 * Go through timeout list (for this task only) and remove the first matching
 * entry, even though the timeout has not triggered yet.
 *
 * @note This function only works as expected if there is only one timeout
 * calling 'handler' in the list of timeouts.
 *
 * @param handler callback function that would be called by the timeout
 * @param arg callback argument that would be passed to handler
*/
void
sys_untimeout(alarm_handler handler, void *arg)
{
}

#if 0

/** Handle timeouts for NO_SYS==1 (i.e. without using
 * tcpip_thread/sys_timeouts_mbox_fetch(). Uses sys_now() to call timeout
 * handler functions when timeouts expire.
 *
 * Must be called periodically from your main loop.
 */
void
sys_check_timeouts(void)
{
  struct sys_timeo *tmptimeout;
  uint32_t diff;
  sys_timeout_handler handler;
  void *arg;
  int had_one;
  uint32_t now;

  now = sys_now();
  if (next_timeout) {
    /* this cares for wraparounds */
    diff = LWIP_U32_DIFF(now, timeouts_last_time);
    do
    {
      had_one = 0;
      tmptimeout = next_timeout;
      if (tmptimeout->time <= diff) {
        /* timeout has expired */
        had_one = 1;
        timeouts_last_time = now;
        diff -= tmptimeout->time;
        next_timeout = tmptimeout->next;
        handler = tmptimeout->h;
        arg = tmptimeout->arg;
#if LWIP_DEBUG_TIMERNAMES
        if (handler != NULL) {
          LWIP_DEBUGF(TIMERS_DEBUG, ("sct calling h=%s arg=%p\n",
            tmptimeout->handler_name, arg));
        }
#endif /* LWIP_DEBUG_TIMERNAMES */
        memp_free(MEMP_SYS_TIMEOUT, tmptimeout);
        if (handler != NULL) {
          handler(arg);
        }
      }
    /* repeat until all expired timers have been called */
    }while(had_one);
  }
}

/** Set back the timestamp of the last call to sys_check_timeouts()
 * This is necessary if sys_check_timeouts() hasn't been called for a long
 * time (e.g. while saving energy) to prevent all timer functions of that
 * period being called.
 */
void
sys_restart_timeouts(void)
{
  timeouts_last_time = sys_now();
}

// #else /* NO_SYS */

/**
 * Wait (forever) for a message to arrive in an mbox.
 * While waiting, timeouts are processed.
 *
 * @param mbox the mbox to fetch the message from
 * @param msg the place to store the message
 */
void
sys_timeouts_mbox_fetch(sys_mbox_t *mbox, void **msg)
{
  uint32_t time_needed;
  struct sys_timeo *tmptimeout;
  sys_timeout_handler handler;
  void *arg;

 again:
  if (!next_timeout) {
    time_needed = sys_arch_mbox_fetch(mbox, msg, 0);
  } else {
    if (next_timeout->time > 0) {
      time_needed = sys_arch_mbox_fetch(mbox, msg, next_timeout->time);
    } else {
      time_needed = SYS_ARCH_TIMEOUT;
    }

    if (time_needed == SYS_ARCH_TIMEOUT) {
      /* If time == SYS_ARCH_TIMEOUT, a timeout occured before a message
         could be fetched. We should now call the timeout handler and
         deallocate the memory allocated for the timeout. */
      tmptimeout = next_timeout;
      next_timeout = tmptimeout->next;
      handler = tmptimeout->h;
      arg = tmptimeout->arg;
#if LWIP_DEBUG_TIMERNAMES
      if (handler != NULL) {
        LWIP_DEBUGF(TIMERS_DEBUG, ("stmf calling h=%s arg=%p\n",
          tmptimeout->handler_name, arg));
      }
#endif /* LWIP_DEBUG_TIMERNAMES */
      memp_free(MEMP_SYS_TIMEOUT, tmptimeout);
      if (handler != NULL) {
        /* For LWIP_TCPIP_CORE_LOCKING, lock the core before calling the
           timeout handler function. */
        LOCK_TCPIP_CORE();
        handler(arg);
        UNLOCK_TCPIP_CORE();
      }
      LWIP_TCPIP_THREAD_ALIVE();

      /* We try again to fetch a message from the mbox. */
      goto again;
    } else {
      /* If time != SYS_ARCH_TIMEOUT, a message was received before the timeout
         occured. The time variable is set to the number of
         milliseconds we waited for the message. */
      if (time_needed < next_timeout->time) {
        next_timeout->time -= time_needed;
      } else {
        next_timeout->time = 0;
      }
    }
  }
}

#endif
