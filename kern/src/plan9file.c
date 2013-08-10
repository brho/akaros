/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */
#define DEBUG
#include <setjmp.h>
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
#include <fcall.h>

int
openmode(int omode, struct errbuf *perrbuf)
{
	omode &= ~(OTRUNC|OCEXEC|ORCLOSE);
	if(omode > OEXEC)
	  error(Ebadarg);
	if(omode == OEXEC)
		return OREAD;
	return omode;
}

static void
unlockfgrp(struct fgrp *f)
{
  int ex;
  
  ex = f->exceed;
  f->exceed = 0;
  spin_unlock(&f->lock);
  if(ex)
    printd("warning: process exceeds %d file descriptors\n", ex);
}


int
growfd(struct fgrp *f, int fd)	/* fd is always >= 0 */
{
    struct chan **newfd, **oldfd;
    
    if(fd < f->nfd)
	return 0;
    if(fd >= f->nfd+DELTAFD)
		return -1;	/* out of range */
    /*
     * Unbounded allocation is unwise
     */
    if(f->nfd >= 5000){
    Exhausted:
	printd("no free file descriptors\n");
	return -1;
    }
    newfd = kmalloc((f->nfd+DELTAFD)*sizeof(struct chan*), KMALLOC_WAIT);
    if(newfd == 0)
	goto Exhausted;
    oldfd = f->fd;
    memmove(newfd, oldfd, f->nfd*sizeof(struct chan*));
    f->fd = newfd;
    kfree(oldfd);
    f->nfd += DELTAFD;
    if(fd > f->maxfd){
	if(fd/100 > f->maxfd/100)
	    f->exceed = (fd/100)*100;
	f->maxfd = fd;
    }
    return 1;
}

/*
 *  this assumes that the fgrp is locked
 */
int
findfreefd(struct fgrp *f, int start)
{
	int fd;

	for(fd=start; fd<f->nfd; fd++)
		if(f->fd[fd] == 0)
			break;
	if(fd >= f->nfd && growfd(f, fd) < 0)
		return -1;
	return fd;
}

int
newfd(struct proc *up, struct chan *c)
{
	int fd;
	struct fgrp *f;

	f = up->fgrp;
	spin_lock(&f->lock);
	fd = findfreefd(f, 0);
	if(fd < 0){
		unlockfgrp(f);
		return -1;
	}
	if(fd > f->maxfd)
		f->maxfd = fd;
	f->fd[fd] = c;
	unlockfgrp(f);
	return fd;
}

int
sysopen(struct proc *up, char *name, int omode)
{
    PERRBUF;
    ERRSTACK(2);
    struct chan *c = NULL;
    int fd;
printd("sysopen call waserror\n");
	if (waserror()){
		panic("sysopen waserror");
	    if(c)
		cclose(c, perrbuf);
	    printd("bad mode %x\n", omode);
	    return -1;
	}

printd("sysopen call openmode\n");
	openmode(omode, perrbuf);	/* error check only */

	printd("sysopen call namec %s \n", name);
	c = namec(up, name, Aopen, omode, 0, perrbuf);
	printd("namec returns %p\n", c);
	fd = newfd(up,c);
	if(fd < 0)
	    error(Enofd);
	return fd;
}

