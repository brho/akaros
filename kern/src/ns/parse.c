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


/*
 * Generous estimate of number of fields, including terminal NULL pointer
 */
static int
ncmdfield(char *p, int n)
{
	int white, nwhite;
	char *ep;
	int nf;

	if(p == NULL)
		return 1;

	nf = 0;
	ep = p+n;
	white = 1;	/* first text will start field */
	while(p < ep){
		nwhite = (strchr(" \t\r\n", *p++ & 0xFF) != 0);	/* UTF is irrelevant */
		if(white && !nwhite)	/* beginning of field */
			nf++;
		white = nwhite;
	}
	return nf+1;	/* +1 for NULL */
}

/*
 *  parse a command written to a device
 */
struct cmdbuf*
parsecmd(char *p, int n)
{
	ERRSTACK(2);
	struct cmdbuf *volatile cb;
	int nf;
	char *sp;

	nf = ncmdfield(p, n);

	/* allocate struct cmdbuf plus string pointers plus copy of string including \0 */
	sp = kzmalloc(sizeof(*cb) + nf * sizeof(char *) + n + 1, 0);
	cb = (struct cmdbuf*)sp;
	cb->f = (char**)(&cb[1]);
	cb->buf = (char*)(&cb->f[nf]);

	if(current!=NULL && waserror()){
		kfree(cb);
		nexterror();
	}
	memmove(cb->buf, p, n);
	if(current != NULL)
		poperror();

	/* dump new line and null terminate */
	if(n > 0 && cb->buf[n-1] == '\n')
		n--;
	cb->buf[n] = '\0';

	cb->nf = tokenize(cb->buf, cb->f, nf-1);
	cb->f[cb->nf] = NULL;

	return cb;
}

/*
 * Reconstruct original message, for error diagnostic
 */
void
cmderror(struct cmdbuf *cb, char *s)
{
	int i;
	char *p, *e;

	p = current->genbuf;
	e = p+ERRMAX-10;
	p += cprintf(p, e, "%s \"", s);
	for(i=0; i<cb->nf; i++){
		if(i > 0)
			p += cprintf(p, e, " ");
		p += cprintf(p, e, "%q", cb->f[i]);
	}
	strncpy(p,  "\"", sizeof(p));
	error(current->genbuf);
}

/*
 * Look up entry in table
 */
struct cmdtab*
lookupcmd(struct cmdbuf *cb, struct cmdtab *ctab, int nctab)
{
	int i;
	struct cmdtab *ct;

	if(cb->nf == 0)
		error("empty control message");

	for(ct = ctab, i=0; i<nctab; i++, ct++){
		if(strcmp(ct->cmd, "*") !=0)	/* wildcard always matches */
		if(strcmp(ct->cmd, cb->f[0]) != 0)
			continue;
		if(ct->narg != 0 && ct->narg != cb->nf)
			cmderror(cb, Ecmdargs);
		return ct;
	}

	cmderror(cb, "unknown control message");
	return NULL;
}
