#include <stdlib.h>
#include <stdio.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <signal.h>

int main(int argc, char **argv)
{
	struct event_msg msg = {0};
	int pid, ev_type;

	if (argc < 3) {
		printf("Usage: %s PID EV_NUM [Arg1 Arg2 0xArg3 Arg4]\n",
		       argv[0]);
		exit(-1);
	}
	pid = strtol(argv[1], 0, 10);
	ev_type = strtol(argv[2], 0, 10);
	msg.ev_type = ev_type;

	if (argc >= 4)
		msg.ev_arg1 = strtol(argv[3], 0, 10);
	if (argc >= 5)
		msg.ev_arg2 = strtol(argv[4], 0, 10);
	if (argc >= 6)
		msg.ev_arg3 = (void*)strtoll(argv[5], 0, 16);	/* base 16 */
	if (argc >= 7)
		msg.ev_arg4 = strtoll(argv[6], 0, 10);

	if (sys_notify(pid, ev_type, &msg)) {
		perror("Notify failed");
		exit(errno);
	}
	return 0;
}
