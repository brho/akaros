/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/debug.h"

#include "lwip/sys.h"
#include "lwip/opt.h"
#include "lwip/stats.h"


void sys_init(void) {
	printf("TODO: SYS_INIT\n");
	return;
}

u32_t sys_now(void) {
	printf("TODO: SYS_NOW\n");
	return 0xDEADBEEF;
}

sys_sem_t sys_sem_new(u8_t count) {
	sys_sem_t ret = 0;
	printf("TODO: SYS_SEM_NEW\n");
	return ret;
}


void sys_sem_free(sys_sem_t sem) {
	printf("TODO: SYS_SEM_FREE\n");
	return;
}


void sys_sem_signal(sys_sem_t sem) {
	printf("TODO: SYS_SEM_SIGNAL\n");
	return;
}


u32_t sys_arch_sem_wait(sys_sem_t sem, u32_t timeout) {
	printf("TODO: SYS ARCH SEM WAIT\n");	
	return 0;
}

sys_mbox_t sys_mbox_new(int size) {
	printf("TODO: SYS MBOX NEW\n");
	sys_mbox_t box = 0;
	return box;
}

void sys_mbox_free(sys_mbox_t mbox) {
	printf("TODO: SYS MBOX FREE\n");
	return;
}

void sys_mbox_post(sys_mbox_t mbox, void *msg) {
	printf("TODO: SYS MBOX POST\n");
	return;
}

err_t sys_mbox_trypost(sys_mbox_t mbox, void *msg) {
	printf("TODO: SYS MBOX TRYPOST\n");
	return 0;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t mbox, void **msg, u32_t timeout) {
	printf("TODO: SYS ARCH MBOX FETCH\n");
	return 0;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t mbox, void **msg) {
	printf("TODO: SYS ARCH MBOX TRYFETCH\n");
	return 0;
}

struct sys_timeouts *sys_arch_timeouts(void) {
	printf("TODO: SYS_TIMEOUTS\n");
	struct sys_timeouts *ret = 0;
	return ret;
}

sys_thread_t sys_thread_new(char *name, void (* thread)(void *arg), void *arg, int stacksize, int prio) {
	printf("TODO: SYS THREAD NEW\n");
	sys_thread_t ret = 0;
	return ret;
}

sys_prot_t sys_arch_protect(void) {
	printf("TODO: SYS ARCH PROTECT\n");
	sys_prot_t ret = 0;
	return ret;
}

void sys_arch_unprotect(sys_prot_t pval) {
	printf("TODO: SYS ARCH UNPROTECT\n");
	return;
}

