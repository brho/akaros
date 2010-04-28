/* tests/msr_dumb_while.c
 *
 * This requests the max_vcores in the system, then just while loops in a
 * userthread.  The pthread code will nicely yield if it detects an incoming
 * preemption. */

#include <ros/notification.h>
#include <stdlib.h>
#include <vcore.h>
#include <pthread.h>
#include <rassert.h>

void *while_thread(void *arg)
{
	while (1);
}

int main(int argc, char** argv)
{
	pthread_t *my_threads = malloc(sizeof(pthread_t) * max_vcores());

	/* set up to receive the PREEMPT_PENDING notif */
	struct notif_method *nm;
	nm = &__procdata.notif_methods[NE_PREEMPT_PENDING];
	nm->flags |= NOTIF_WANTED | NOTIF_IPI;

	/* actually only need one less, since the _S will be pthread 0 */
	for (int i = 0; i < max_vcores() - 1; i++)
		pthread_create(&my_threads[i], NULL, &while_thread, NULL);

	assert(num_vcores() == max_vcores());
	while (1);

	/* should never make it here */
	return -1;
}
