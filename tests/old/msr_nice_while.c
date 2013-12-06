/* tests/msr_dumb_while.c
 *
 * This requests the max_vcores in the system, then just while loops in a
 * userthread.  The pthread code will nicely yield if it detects an incoming
 * preemption. */

#include <stdlib.h>
#include <vcore.h>
#include <pthread.h>
#include <rassert.h>
#include <event.h>
#include <uthread.h>

void *while_thread(void *arg)
{
	while (1);
}

int main(int argc, char** argv)
{
	pthread_t *my_threads = malloc(sizeof(pthread_t) * max_vcores());

	/* set up to receive the PREEMPT_PENDING event.  EVENT_VCORE_APPRO tells the
	 * kernel to send the msg to whichever vcore is appropriate.  Pthread code
	 * will see the preemption and yield. */
	struct event_queue *ev_q = get_event_q();
	ev_q->ev_flags = EVENT_IPI | EVENT_NOMSG | EVENT_VCORE_APPRO;
	register_kevent_q(ev_q, EV_PREEMPT_PENDING);

	/* actually only need one less, since the _S will be pthread 0 */
	for (int i = 0; i < max_vcores() - 1; i++)
		pthread_create(&my_threads[i], NULL, &while_thread, NULL);

	assert(num_vcores() == max_vcores());
	while (1);

	/* should never make it here */
	return -1;
}
