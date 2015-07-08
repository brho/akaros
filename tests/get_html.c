#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <iplib/iplib.h>
#include <parlib/net.h>

/* simple test, gets a single web page.  no url parsing, no timeout detection,
 * etc.  pass it the IP addr and page to fetch.
 *
 * check out http://www.d.umn.edu/~gshute/net/http-script.html for some info. */
int main(int argc, char *argv[])
{
	char *host, *page, *port;
	int dfd, ret;
	char buf[256];
	char addr[256];
	host = "146.148.59.222";
	page = "files/test.html";
	port = "80";

	if (argc > 1)
		host = argv[1];
	if (argc > 2)
		port = argv[2];
	if (argc > 3)
		page = argv[3];

	printf("FYI, Usage: %s [HOST [PORT [PAGE]]]\n", argv[0]);

	printf("Trying to access http://%s:%s/%s\n", host, port, page);
	/* manually making our own addr (no mkaddr, which was racy anyway) */
	ret = snprintf(addr, sizeof(addr), "tcp!%s!%s", host, port);
	if (snprintf_overflow(ret, addr, sizeof(addr))) {
		perror("Addr string too long");
		exit(-1);
	}
	dfd = dial(addr, 0, 0, 0);
	if (dfd < 0) {
		perror("Bad Data FD");
		exit(-1);
	}
	/* short get style */
	snprintf(buf, sizeof(buf), "GET /%s\r\nConnection: close\r\n\r\n", page);
	ret = write(dfd, buf, strlen(buf));
	if (ret < 0) {
		perror("Write");
		exit(-1);
	}
	/* buf - 1, to leave room for a \0 when we print */
	while ((ret = read(dfd, buf, sizeof(buf) - 1)) > 0) {
		assert(ret < sizeof(buf));
		/* trim to print only what we received */
		buf[ret] = 0;
		printf("%s", buf);
	}
}
