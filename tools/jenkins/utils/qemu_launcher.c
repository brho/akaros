#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

pid_t pid;

void signal_handler(int signal) {
	int res = kill(pid, SIGKILL);
}

int main(int argc, char *argv[])
{
	pid = fork();

	if (pid == -1) {
		perror("Fork failed when trying to spawn qemu process.\n");
		return 1;
	} else if (pid == 0) { /* Child process */
		char* prog_name = "qemu-system-x86_64";
		char* params[argc];
		int i;

		params[0] = prog_name;
		for (i = 1; i < argc; ++i)
		{
			params[i] = argv[i];
		}
		params[argc] = NULL;

		execvp(prog_name, params);
	} else { /* Parent process */
		int status;

		if (signal(SIGUSR1, signal_handler) == SIG_ERR) {
			fputs("An error occurred while setting a signal handler.\n", stderr);
			return 2;
		}

		(void) waitpid(pid, &status, 0);
	}

	return 0;
}
