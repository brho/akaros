#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <ros/procdata.h>
#include <ros/event.h>
#include <ros/bcq.h>
#include <arch/arch.h>
#include <stdio.h>
#include <vcore.h>
#include <mcs.h>
#include <timing.h>
#include <rassert.h>
#include <event.h>
#include <uthread.h>

#ifdef __sparc_v8__
# define udelay(x) udelay((x)/2000)
#endif

mcs_barrier_t b;

__thread int temp;
void *core0_tls = 0;

struct event_queue *indirect_q;
static void handle_generic(struct event_msg *ev_msg, unsigned int ev_type,
                           bool overflow);

void ghetto_vcore_entry(void);
struct uthread *ghetto_init(void)
{
	struct uthread *uthread = malloc(sizeof(struct uthread));
	memset(uthread, 0, sizeof(struct uthread));
	return uthread;
}

struct uthread *ghetto_create(void (*func)(void), void *data)
{
	return ghetto_init();
}

struct schedule_ops ghetto_sched_ops = {
	.sched_init = ghetto_init,
	.sched_entry = ghetto_vcore_entry,
	.thread_create = ghetto_create,
};
struct schedule_ops *sched_ops = &ghetto_sched_ops;

/* to trick uthread_create() */
void dummy(void)
{
}

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	int retval;

	mcs_barrier_init(&b, max_vcores());

	/* vcore_context test */
	assert(!in_vcore_context());
	
	/* prep indirect ev_q.  Note we grab a big one */
	indirect_q = get_big_event_q();
	indirect_q->ev_flags = EVENT_IPI;
	indirect_q->ev_vcore = 1;			/* IPI core 1 */
	indirect_q->ev_handler = 0;
	printf("Registering %08p for event type %d\n", indirect_q,
	       EV_FREE_APPLE_PIE);
	register_kevent_q(indirect_q, EV_FREE_APPLE_PIE);

	/* handle events: just want to print out what we get.  This is just a
	 * quick set of handlers, not a registration for a kevent. */
	for (int i = 0; i < MAX_NR_EVENT; i++)
		ev_handlers[i] = handle_generic;
	/* Want to use the default ev_ev (which we just overwrote) */
	ev_handlers[EV_EVENT] = handle_ev_ev;
	/* vcore_init() done in vcore_request() now. */
	/* Set up event reception.  For example, this will allow us to receive an
	 * event and IPI for USER_IPIs on vcore 0.  Check event.c for more stuff.
	 * Note you don't have to register for USER_IPIs to receive ones you send
	 * yourself with sys_self_notify(). */
	enable_kevent(EV_USER_IPI, 0, EVENT_IPI);
	/* Receive pending preemption events.  Can also get a MSG if you want. */
	struct event_queue *ev_q = get_event_q();
	ev_q->ev_flags = EVENT_IPI | EVENT_NOMSG | EVENT_VCORE_APPRO;
	register_kevent_q(ev_q, EV_PREEMPT_PENDING);

	/* Makes a thread for us, though we won't use it.  Just a hack to get into
	 * _M mode.  Note this requests one vcore for us */
	uthread_create(dummy, 0);

	if ((vcoreid = vcore_id())) {
		printf("Should never see me! (from vcore %d)\n", vcoreid);
	} else { // core 0
		temp = 0xdeadbeef;
		printf("Hello from vcore %d with temp addr = %p and temp = %p\n",
		       vcoreid, &temp, temp);
		printf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		//retval = sys_resource_req(RES_CORES, 2, 0);
		printf("Requesting %d vcores\n", max_vcores() - 1);
		retval = vcore_request(max_vcores() - 1); /* since we already have 1 */
		//retval = vcore_request(5);
		printf("This is vcore0, right after vcore_request, retval=%d\n", retval);
		/* vcore_context test */
		assert(!in_vcore_context());
	}

	/* test notifying my vcore2 */
	udelay(5000000);
	printf("Vcore 0 self-notifying vcore 2 with notif 4!\n");
	struct event_msg msg;
	msg.ev_type = 4;
	sys_self_notify(2, 4, &msg);
	udelay(5000000);
	printf("Vcore 0 notifying itself with notif 3!\n");
	msg.ev_type = 3;
	sys_notify(sys_getpid(), 3, &msg);
	udelay(1000000);

	/* test loop for restarting a notif_tf */
	if (vcoreid == 0) {
		int ctr = 0;
		while(1) {
			printf("Vcore %d Spinning (%d), temp = %08x!\n", vcoreid, ctr++, temp);
			udelay(5000000);
			//exit(0);
		}
	}

	printf("Vcore %d Done!\n", vcoreid);
	//mcs_barrier_wait(&b,vcore_id());

	printf("All Cores Done!\n", vcoreid);
	while(1); // manually kill from the monitor
	return 0;
}

static void handle_generic(struct event_msg *ev_msg, unsigned int ev_type,
                           bool overflow)
{
	printf("Got event type %d on vcore %d, with%s overflow\n",
	       ev_type, vcore_id(), overflow ? "" : "out");
}

void ghetto_vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();
	static bool first_time = TRUE;

	temp = 0xcafebabe;
	/* vcore_context test (don't need to do this anywhere) */
	assert(in_vcore_context());

	/* old logic was moved to parlib code */
	if (current_uthread) {
		assert(vcoreid == 0);
		run_current_uthread();
	}
	/* unmask notifications once you can let go of the notif_tf and it is okay
	 * to clobber the transition stack.
	 * Check Documentation/processes.txt: 4.2.4.  In real code, you should be
	 * popping the tf of whatever user process you want (get off the x-stack) */
	enable_notifs(vcoreid);

/* end: stuff userspace needs to do to handle notifications */

	printf("Hello from vcore_entry in vcore %d with temp addr %p and temp %p\n",
	       vcoreid, &temp, temp);
	vcore_request(1);
	//mcs_barrier_wait(&b,vcore_id());
	udelay(vcoreid * 10000000);
	//exit(0);
	while(1);
}
