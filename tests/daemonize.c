#include <parlib/event.h>
#include <parlib/parlib.h>
#include <parlib/uthread.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void ev_handler(struct event_msg *msg, unsigned int ev_type, void *data)
{
	int rv;

	assert(msg != NULL);
	assert(ev_type == EV_USER_IPI);
	(void)data;
	rv = msg->ev_arg1;
	exit(rv);
}

int main(int argc, char *argv[], char *envp[])
{
	struct event_queue *evq, *triggered;
	pid_t pid;
	struct event_msg msg;

	register_ev_handler(EV_USER_IPI, ev_handler, 0);
	evq = get_eventq(EV_MBOX_UCQ);
	evq->ev_flags |= EVENT_IPI | EVENT_INDIR | EVENT_SPAM_INDIR | EVENT_WAKEUP;
	register_kevent_q(evq, EV_USER_IPI);

	pid = create_child_with_stdfds(argv[1], argc - 1, argv + 1, envp);
	if (pid < 0) {
		perror("child creation failed");
		exit(-1);
	}
	sys_proc_run(pid);

	uthread_sleep_forever();

	return -1;
}
