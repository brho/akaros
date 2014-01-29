// INFERNO
#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>

static char qsep[] = " \t\r\n";

static char *qtoken(char *s)
{
	int quoting;
	char *t;

	quoting = 0;
	t = s;	/* s is output string, t is input string */
	while (*t != '\0' && (quoting || /*utfrune */ strchr(qsep, *t) == NULL)) {
		if (*t != '\'') {
			*s++ = *t++;
			continue;
		}
		/* *t is a quote */
		if (!quoting) {
			quoting = 1;
			t++;
			continue;
		}
		/* quoting and we're on a quote */
		if (t[1] != '\'') {
			/* end of quoted section; absorb closing quote */
			t++;
			quoting = 0;
			continue;
		}
		/* doubled quote; fold one quote into two */
		t++;
		*s++ = *t++;
	}
	if (*s != '\0') {
		*s = '\0';
		if (t == s)
			t++;
	}
	return t;
}

int tokenize(char *s, char **args, int maxargs)
{
	int nargs;

	for (nargs = 0; nargs < maxargs; nargs++) {
		while (*s != '\0' && /*utfrune */ strchr(qsep, *s) != NULL)
			s++;
		if (*s == '\0')
			break;
		args[nargs] = s;
		s = qtoken(s);
	}

	return nargs;
}
