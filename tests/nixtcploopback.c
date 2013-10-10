/* ping for ip v4 and v6 */
#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <nixip.h>
#include <icmp.h>
#include <ctype.h>

/* see http://swtch.com/plan9port/man/man3/dial.html */
void caller(char *addr)
{
	char data[512];
	printf("Caller %d dialing %s\n", getpid(), addr);
	int fd = dial(addr, 0, 0, 0);
	printf("Caller %d, dialed %s and got %d\n", getpid(), addr, fd);
	while (1){
		int amt;
		printf("Caller about to read\n");
		amt = read(0, data, sizeof(data));
		if (amt <= 0)
			break;
		printf("Caller read %s, about to write it to %d\n", data, fd);
		amt = write(fd, data, amt);
		printf("Caller wrote, got %d back.  will read from %d\n", amt, fd);
		amt = read(fd, data, sizeof(data));
		printf("Caller read %d bytes from %d\n", amt, fd);
		if (amt <= 0)
			break;
		write(1, data, amt);
	}
	printf("Caller spinning\n");
	while (1)
		sleep(10);
}

int server(char *addr)
{
	int dfd, acfd, lcfd;
	char adir[40], ldir[40];
	int n;
	char buf[256];
	acfd = announce(addr, adir);
	printf("\tServer announced, got acfd %d\n", acfd);
	if (acfd < 0)
		return -1;
	for (;;) {
/* listen for a call */
		printf("\tServer listening\n", lcfd);
		lcfd = listen(adir, ldir);
		printf("\tServer listened, got lcfd %d\n", lcfd);
		if (lcfd < 0)
			return -1;
/* fork a process to echo */
		switch (fork()) {
			case -1:
				printf("\tServer fork failed\n");
				perror("forking");
				exit(1);
				break;
			case 0:
				printf("\t\tserver forked, in child %d\n", getpid());
/* accept the call and open the data file */
				dfd = accept(lcfd, ldir);
				printf("\t\tserver child %d accept dfd %d\n", getpid(), dfd);
				if (dfd < 0)
					return -1;
/* echo until EOF */
				printf("\t\tserver child %d about to read\n", getpid());
				while ((n = read(dfd, buf, sizeof(buf))) > 0) {
					printf("\t\tserver child %d read %d, about to write\n",
					       getpid(), n);
					write(dfd, buf, n);
					printf("\t\tserver child %d about to read\n", getpid());
				}
				exit(0);
			default:
				printf("\tServer parent %d, closing lcdf %d\n", getpid(), lcfd);
				close(lcfd);
				break;
		}
	}
}

void main(int argc, char **argv)
{
	int fd, msglen, interval, nmsg;
	char *ds;
	int pid;
	char *saddr = "/9/net/tcp!127.0.0.1!2000";
	char *caddr = "/9/net/tcp!127.0.0.1!2000";

	if (argc > 1)
		saddr = argv[1];
	if (argc > 2)
		saddr = argv[2];
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid == 0){
		for (int i = 0; i < 3; i++)
			sys_block(1000000);
		caller(caddr);
	} else
		server(saddr);
}
