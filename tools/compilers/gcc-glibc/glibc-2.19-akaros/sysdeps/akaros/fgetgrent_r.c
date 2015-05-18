#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <stdio.h>

#include <libio/iolibio.h>

int __fgetgrent_r(FILE *stream, struct group *resbuf, char *buffer,
                  size_t buflen, struct group **result)
{
	return 0;
}
weak_alias(__fgetgrent_r, fgetgrent_r);
stub_warning(__fgetgrent_r);
