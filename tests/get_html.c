#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <net.h>

/* simple test, gets a single web page.  no url parsing, no timeout detection,
 * etc.  pass it the IP addr and page to fetch.
 *
 * check out http://www.d.umn.edu/~gshute/net/http-script.html for some info. */
int main(int argc, char *argv[])
{
	char *host, *page;
	int dfd, ret;
	char buf[128];
	char addr[256];
	if (argc != 3) {
		printf("Usage: %s HOST PAGE\n", argv[0]);
		host = "128.32.37.180";
		page = "files/test.html";
	} else {
		host = argv[1];
		page = argv[2];
	}
	printf("Trying to access http://%s/%s\n", host, page);
	/* manually making our own addr (no mkaddr, which was racy anyway) */
	ret = snprintf(addr, sizeof(addr), "tcp!%s!%s", host, "80");
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
	snprintf(buf, sizeof(buf), "GET /%s\n\n", page);
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
