/*
t * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
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
#include "netif/ethernetif.h"

#include <pthread.h>
#include <arch/arch.h>

#define sys_debug(...) //printf(__VA_ARGS__)

pthread_mutex_t sys_lock;

uint8_t protection_status;

__thread struct sys_timeouts local_timeouts;

typedef struct sys_hart_startup {

        void (*hart_startup)(void* arg);
        void *arg;

} sys_hart_startup_t;


void  sys_recv_thread(void) {

	while(1) {

		if (sys_eth_recv_check() == 1) {

			extern struct netif* registered_netif;

			ethernetif_input(registered_netif);
		}
	}

}

// HACK
void sys_init(void) {
	sys_debug("In sys_init\n");

	pthread_mutex_init(&sys_lock, NULL);
	
	protection_status = 0;

	extern void (*hart_startup)();
	extern void *hart_startup_arg;

        hart_startup = sys_recv_thread;
        hart_startup_arg = NULL;

        hart_request(1);
}

// HACK
u32_t sys_now(void) {

	sys_debug("In sys_now\n");

	uint64_t now = read_tsc();

	now = now / procinfo.tsc_freq;

	now = now * 1000;

	return (uint32_t)now;
}

// OK
sys_sem_t sys_sem_new(u8_t count) {

	sys_debug("In sys_sem_new\n");

	sys_sem_t sem = (sys_sem_t)malloc(sizeof(struct sys_sem));

	if (sem == NULL)
		return SYS_SEM_NULL;

	pthread_mutex_init(&(sem->lock), NULL);

	sem->count = count;

	return sem;
}

// OK
void sys_sem_free(sys_sem_t sem) {

	sys_debug("In sys_sem_free\n");

	pthread_mutex_destroy(&(sem->lock));	

	free(sem);

	return;
}

// OK
void sys_sem_signal(sys_sem_t sem) {

	sys_debug("In sys_sem_signal. Signal on %x\n", sem);

	pthread_mutex_lock(&(sem->lock));

	sem->count = sem->count + 1;

	pthread_mutex_unlock(&(sem->lock));
	
	return;
}

// OK
u32_t sys_arch_sem_wait(sys_sem_t sem, u32_t timeout) {

	sys_debug("In sys_arch_sem_wait. Wait on sem\n");

	uint32_t start = sys_now();
	uint32_t current = 0;

	pthread_mutex_lock(&(sem->lock));

	while (sem->count == 0) {

		pthread_mutex_unlock(&(sem->lock));

		current = sys_now();
		
		if (((current - start) > timeout) && (timeout != 0)) {
			return SYS_ARCH_TIMEOUT;
		}

		hart_relax();
		
		pthread_mutex_lock(&(sem->lock));
	}

	sem->count = sem->count - 1;

	pthread_mutex_unlock(&(sem->lock));

	return sys_now() - start;
}


//HACK
sys_mbox_t sys_mbox_new(int size) {

	sys_debug("In sys_mbox_new\n");

	if (size == 0) {
		printf("DANGER. BAD MBOX SIZE\n");
		size = 20;
	}

	sys_mbox_t new_box = (sys_mbox_t)malloc(sizeof(struct sys_mbox) + size * sizeof(char*));

	if (new_box == NULL)
		return SYS_MBOX_NULL;
	memset(new_box, 0x00, sizeof(struct sys_mbox) + size * sizeof(char*));

	pthread_mutex_init(&(new_box->lock), NULL);

	new_box->size = size;
	new_box->count = 0;
	new_box->first = 0;
	
	return new_box;
}

// HACK
void sys_mbox_free(sys_mbox_t mbox) {

	sys_debug("In sys_mbox_new\n");

	// Should we aquire the lock here?
	if (mbox->count != 0) {
		printf("LWIP Stack errror. Bad.\n");
		return;
	}

	pthread_mutex_destroy(&(mbox->lock));

	free(mbox);

	return;
}


// HACK
void sys_mbox_post(sys_mbox_t mbox, void *msg) {

	sys_debug("In sys_mbox_post. Post on %x\n", mbox);

	pthread_mutex_lock(&(mbox->lock));

	while(mbox->count >= mbox->size) {
		
		pthread_mutex_unlock(&(mbox->lock));
		
		hart_relax();
		
		pthread_mutex_lock(&(mbox->lock));
	}

	mbox->buf[(mbox->first + mbox->count) % mbox->size] = msg;
	mbox->count = mbox->count + 1;
	
	pthread_mutex_unlock(&(mbox->lock));

	return;
}

// HACK
err_t sys_mbox_trypost(sys_mbox_t mbox, void *msg) {

	sys_debug("In sys_mbox_trypost. Post on %x\n", mbox);

	pthread_mutex_lock(&(mbox->lock));

	if (mbox->count >= mbox->size) {
		
		pthread_mutex_unlock(&(mbox->lock));

		return ERR_MEM;
	}

	mbox->buf[(mbox->first + mbox->count) % mbox->size] = msg;
	mbox->count = mbox->count + 1;

	pthread_mutex_unlock(&(mbox->lock));

	return ERR_OK;
}

// HACK
u32_t sys_arch_mbox_fetch(sys_mbox_t mbox, void **msg, u32_t timeout) {

	sys_debug("In sys_arch_mbox_fetch. Fetch on mbox %x\n", mbox);

	uint32_t start = sys_now();
	uint32_t current = 0;

	pthread_mutex_lock(&(mbox->lock));

        while (mbox->count == 0) {

                pthread_mutex_unlock(&(mbox->lock));

                current = sys_now();

                if (((current - start) > timeout) && (timeout != 0)) {
                        return SYS_ARCH_TIMEOUT;
                }

                hart_relax();

                pthread_mutex_lock(&(mbox->lock));

        }

	*msg = mbox->buf[mbox->first];

	mbox->first = (mbox->first + 1) % mbox->size;

        mbox->count = mbox->count - 1;

        pthread_mutex_unlock(&(mbox->lock));

        return sys_now() - start;
}

// HACK
u32_t sys_arch_mbox_tryfetch(sys_mbox_t mbox, void **msg) {

	sys_debug("In sys_arch_mbox_tryfetch. Fetch on %x\n", &mbox);

	pthread_mutex_lock(&(mbox->lock));

        if (mbox->count == 0) {

                pthread_mutex_unlock(&(mbox->lock));

		return SYS_MBOX_EMPTY;
        }

	*msg = mbox->buf[mbox->first];

	mbox->first = (mbox->first + 1) % (mbox->size);

        mbox->count = mbox->count - 1;

        pthread_mutex_unlock(&(mbox->lock));

        return 0;
}

// HACK
struct sys_timeouts *sys_arch_timeouts(void) {

	sys_debug("In sys_timeouts\n");

	return &local_timeouts;
}

void sys_thread_wrapper(void *arg) {

	sys_hart_startup_t* ptr = arg;

	local_timeouts.next = NULL;

	ptr->hart_startup(ptr->arg);

	free(ptr);
}

// HACK
sys_thread_t sys_thread_new(char *name, void (* thread)(void *arg), void *arg, int stacksize, int prio) {

	sys_debug("In sys_thread_new");

	extern void (*hart_startup)();
	extern void *hart_startup_arg;

	sys_hart_startup_t* wrapper_arg = malloc(sizeof(sys_hart_startup_t));

	if (wrapper_arg == NULL)
		return NULL;

	wrapper_arg->hart_startup = thread;
	wrapper_arg->arg = arg;

	hart_startup = sys_thread_wrapper;
	hart_startup_arg = wrapper_arg;

	hart_request(1);

	return 0;
}

// HACK
sys_prot_t sys_arch_protect(void) {

	sys_debug("In sys_arch_protect\n");

	pthread_mutex_lock(&sys_lock);

	sys_prot_t old = protection_status;

	protection_status = 1;

	pthread_mutex_unlock(&sys_lock);

	return old;
}

// HACK
void sys_arch_unprotect(sys_prot_t pval) {

	sys_debug("In sys_arch_unprotect\n");

	pthread_mutex_lock(&sys_lock);

	protection_status = pval;

	pthread_mutex_unlock(&sys_lock);

}

