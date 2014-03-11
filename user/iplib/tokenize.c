/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/*

 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */

#include <string.h>
static char qsep[] = " \t\r\n";

int sep(char *s)
{
	if (strchr(qsep, *s))
		return 1;
	return 0;
}
/* s is output string, t is input string.
 * warning: modifies data in place.
 */
static char*
qtoken(char *s, char *sep)
{
	int quoting;
	char *t;

	quoting = 0;
	t = s;
	while(*t!='\0' && (quoting || (strchr(sep,*t)==NULL))){
		if(*t != '\''){
			*s++ = *t++;
			continue;
		}
		/* *t is a quote */
		if(!quoting){
			quoting = 1;
			t++;
			continue;
		}
		/* quoting and we're on a quote */
		if(t[1] != '\''){
			/* end of quoted section; absorb closing quote */
			t++;
			quoting = 0;
			continue;
		}
		/* doubled quote; fold one quote into two */
		t++;
		*s++ = *t++;
	}
	if(*s != '\0'){
		*s = '\0';
		if(t == s)
			t++;
	}
	return t;
}

static char*
etoken(char *t, char *sep)
{
	int quoting;

	/* move to end of next token */
	quoting = 0;
	while(*t!='\0' && (quoting || (strchr(sep,*t) == NULL))){
		if(*t != '\''){
			t++;
			continue;
		}
		/* *t is a quote */
		if(!quoting){
			quoting = 1;
			t++;
			continue;
		}
		/* quoting and we're on a quote */
		if(t[1] != '\''){
			/* end of quoted section; absorb closing quote */
			t++;
			quoting = 0;
			continue;
		}
		/* doubled quote; fold one quote into two */
		t += 2;
	}
	return t;
}

int
gettokens(char *s, char **args, int maxargs, char *sep)
{
	int nargs;

	for(nargs=0; nargs<maxargs; nargs++){
		while((*s!='\0') && (strchr(sep, *s)!=NULL))
			*s++ = '\0';
		if(*s == '\0')
			break;
		args[nargs] = s;
		s = etoken(s, sep);
	}

	return nargs;
}

int
tokenize(char *s, char **args, int maxargs)
{
	int nargs;

	for(nargs=0; nargs<maxargs; nargs++){
		while((*s!='\0') && sep(s))
			s++;
		if(*s == '\0')
			break;
		args[nargs] = s;
		s = qtoken(s, qsep);
	}

	return nargs;
}
