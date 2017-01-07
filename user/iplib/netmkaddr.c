/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <stdlib.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Makes an address suitable for dialing or announcing.  Takes an address along
 * with a default network and service to use if they are not specified in the
 * address.
 *
 * Returns a pointer to data in buf[buf_sz] holding the actual address to use.
 * The caller manages the memory for buf.
 *
 * If you pass in only one ! in linear, this assumes this ! was between the net
 * and the host.  If you pass in no !s, we'll build one from defnet/defsrv. */
char *netmkaddr(char *linear, char *defnet, char *defsrv, char *buf,
                size_t buf_sz)
{
	char *cp;

	/*
	 *  dump network name
	 */
	cp = strchr(linear, '!');
	if (cp == 0) {
		if (defnet == 0) {
			if (defsrv)
				snprintf(buf, buf_sz, "net!%s!%s", linear, defsrv);
			else
				snprintf(buf, buf_sz, "net!%s", linear);
		} else {
			if (defsrv)
				snprintf(buf, buf_sz, "%s!%s!%s", defnet, linear, defsrv);
			else
				snprintf(buf, buf_sz, "%s!%s", defnet, linear);
		}
		return buf;
	}

	/*
	 *  if there is already a service, use it
	 */
	cp = strchr(cp + 1, '!');
	if (cp)
		return linear;

	/*
	 *  add default service
	 */
	if (defsrv == 0)
		return linear;
	snprintf(buf, buf_sz, "%s!%s", linear, defsrv);

	return buf;
}
