/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Networking helpers for dealing with the plan 9 interface. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <net.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if 0
/* my cheap dial, assumes either /net/ or a protocol first, with !s btw the
 * proto, host, and port.  it also will modify addr temporarily.  */
int dial(char *addr, char *local, char *dir, int *cfdp)
{
	int ret, ctlfd, datafd, conv_id;
	char *prefix;
	char *hostname;						/* including !port */
	size_t buf_len = strlen(addr) + 30;	/* 30 should be enough extra */
	char *buf = malloc(buf_len);
	if (!buf) {
		perror("Unable to malloc buf!");
		return -1;
	}
	if (local || dir) {
		perror("Cheap dial doesn't support local or dir");
		ret = -1;
		goto out_buf;
	}
	hostname = strchr(addr, '!');
	if (!hostname) {
		perror("No first bang");
		ret = -1;
		goto out_buf;
	}
	*hostname++ = '\0';
	prefix = (addr[0] == '/' ? "" : "/net/");
	ret = snprintf(buf, buf_len, "%s%s/clone", prefix, addr);
	if (snprintf_overflow(ret, buf, buf_len)) {
		perror("Clone chan path too long");
		ret = -1;
		goto out_readdr;
	}
	ctlfd = open(buf, O_RDWR);
	if (ctlfd < 0) {
		perror("Can't clone a conversation");
		ret = -1;
		goto out_readdr;
	}
	ret = read(ctlfd, buf, buf_len - 1);
	if (ret <= 0) {
		if (!ret)
			printf("Got early EOF from ctl\n");
		else
			perror("Can't read ctl");
		ret = -1;
		goto out_ctlfd;
	}
	buf[ret] = 0;
	conv_id = atoi(buf);
	ret = snprintf(buf, buf_len, "connect %s", hostname);
	if (snprintf_overflow(ret, buf, buf_len)) {
		perror("Connect string too long");
		ret = -1;
		goto out_ctlfd;
	}
	if ((write(ctlfd, buf, strlen(buf)) <= 0)) {
		perror("Failed to connect");
		ret = -1;
		goto out_ctlfd;
	}
	ret = snprintf(buf, buf_len, "%s%s/%d/data", prefix, addr, conv_id);
	if (snprintf_overflow(ret, buf, buf_len)) {
		perror("Data chan path too long");
		ret = -1;
		goto out_ctlfd;
	}
	datafd = open(buf, O_RDWR);
	if (datafd < 0) {
		perror("Failed to open data chan");
		ret = -1;
		goto out_ctlfd;
	}
	if (cfdp)
		*cfdp = ctlfd;
	else
		close(ctlfd);
	ret = datafd;
	/* skip over the ctlfd close */
	goto out_readdr;

out_ctlfd:
	close(ctlfd);
out_readdr:
	/* restore the change we made to addr */
	*--hostname = '!';
out_buf:
	free(buf);
	return ret;
}
#endif
