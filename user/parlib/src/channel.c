/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#include <parlib.h>
#include <stdio.h>
#include <string.h>
#include <channel.h>
#include <ros/env.h>
#include <ros/syscall.h>
#include <arch/arch.h>

void simulate_rsp(channel_t* ch) {
	channel_t ch_server;
	channel_msg_t msg;
	ch_server.endpoint = ch->endpoint;
	ch_server.type = CHANNEL_SERVER;
	ch_server.ring_addr = ch->ring_addr;
	ch_server.data_addr = ch->data_addr;	
	BACK_RING_INIT(&(ch_server.ring_side.back), (ch_server.ring_addr), PGSIZE);
	
	channel_recvmsg(&ch_server, &msg);
}

error_t channel_create(envid_t server, channel_t* ch, channel_attr_t* ch_attr) {
	error_t e;
	void *COUNT(PGSIZE) ring_addr = NULL;
	void *COUNT(PGSIZE) data_addr = NULL;
	
	/*
	 * Attempt to create two shared pages with the 'server' partition.
	 * One for holding our ring buffer, and one for holding our data.
	 * If there is an error at any point during this process, we need
	 * to cleanup and abort, returning the proper error code.  First we
	 * do the page for the ring buffer, deferring the data page till later.
	 */
	e = sys_shared_page_alloc(&ring_addr, server, PG_RDWR, PG_RDWR);
	if(e < 0) return e;
	
	/* 
	 * If we've made it to here, then we know the shared memory page
	 * for our ring buffer has been mapped successfully into both our own 
	 * address space as well as the server's.  We also know that the 
	 * server has been notified of its creation.  We still haven't mapped in 
	 * our data page, but we will do that after this page has been initialized
	 * properly.  We do this because we need to somehow synchronize the 
	 * the initilization of the ring buffer on this page with 
	 * the server being able to access it.  We do this by forcing the 
	 * initialization now on this end, and only accessing it on the server side
	 * after our data page has been created.
	 */
	memset((void*SAFE) TC(ch), 0, sizeof(channel_t));
	ch->endpoint = server;
	ch->ring_addr = (channel_sring_t *COUNT(1)) TC(ring_addr);
	ch->type = CHANNEL_CLIENT;
		
	/*
	 * As the creator of this channel, we take on the responsibility of 
	 * setting up the ring buffer itself, as well as setting up our own front 
	 * ring so we can push requests (a.ka. messages) out for the server to 
	 * process.  It is the job of the server to setup its back ring so it can
	 * push responses (a.k.a acks) back for us to know the message has been 
	 * processed.
	 */
	SHARED_RING_INIT(ch->ring_addr);
	FRONT_RING_INIT(&(ch->ring_side.front), (ch->ring_addr), PGSIZE);
	
	/* 
	 * Now we map in the data page on both ends and add a pointer to it in 
	 * our channel struct.
	 */
	e = sys_shared_page_alloc(&data_addr, server, PG_RDWR, PG_RDONLY);
	if(e < 0) {
		sys_shared_page_free(ring_addr, server);
		return e;
	}
	ch->data_addr = data_addr;
	
	/* 
	 * Once both pages have been mapped, our data structures have been set up, 
	 * and everything is ready to go, we push an empty message out into the 
	 * ring indicating we are ready on our end.  This implicitly waits for the 
	 * server  to push a response indicating it has finished setting up the other 
	 * side of the channel.
	 */
 	channel_msg_t msg;
	channel_sendmsg(ch, &msg);
    
    /*
     * If everything has gone according to plan, both ends have set up 
     * their respective ends of the channel and we can return successfully
     */
	return ESUCCESS;
}

error_t channel_destroy(channel_t* ch) {
	sys_shared_page_free(ch->data_addr, ch->endpoint);
	sys_shared_page_free((void *COUNT(PGSIZE)) TC(ch->ring_addr), ch->endpoint);
	return ESUCCESS;
}

error_t channel_sendmsg(channel_t* ch, channel_msg_t* msg) {
	/*
	 * As a first cut implementation, just copy the message handed to 
	 * us into our ring buffer and copy the buffer pointed to in 
	 * the message at the first address in our shared data page.
	 */
	channel_msg_t* msg_copy;
	msg_copy = RING_GET_REQUEST(&(ch->ring_side.front), 
	                            ch->ring_side.front.req_prod_pvt++);
	msg_copy->len = msg->len;
	msg_copy->buf = 0;
	memcpy(ch->data_addr + (size_t)(msg_copy->buf), msg->buf, msg->len);
	
	/*
	 * Now that we have copied the message properly, we push the request out
	 * and wait for a response from the server.
	 */
	RING_PUSH_REQUESTS(&(ch->ring_side.front));
	
	while (!(RING_HAS_UNCONSUMED_RESPONSES(&(ch->ring_side.front))))
		cpu_relax();
	RING_GET_RESPONSE(&(ch->ring_side.front), ch->ring_side.front.rsp_cons++);
	
	return ESUCCESS;
}

error_t channel_create_wait(channel_t* ch, channel_attr_t* ch_attr) {
#if 0
	error_t e;
	envid_t* client;
	void *COUNT(PGSIZE) ring_addr = NULL;
	void *COUNT(PGSIZE) data_addr = NULL;

	/* 
	 * Set the type of the channel to the server
	 */
	ch->type = CHANNEL_SERVER;
	
	/*
	 * Wait for the shared ring page to be established and set up
	 * the channel struct with its properties
	 */
	sysevent_shared_page_alloc_wait(client, ring_addr);
	ch->endpoint = *client;
	ch->ring_addr = ring_addr;
	
	/*
	 * Now wait for the shared data page to be established and then set up
	 * the backring of the shared ring buffer.
	 */
	sysevent_shared_page_alloc_wait(client, data_addr);
	ch->data_addr = data_addr;	
	BACK_RING_INIT(&(ch->ring_side.back), (ch->ring_addr), PGSIZE);
	
	/*
	 * If we've reached this point, then the creating side should already
	 * have a message sitting in the ring buffer waiting for use to 
	 * process.  Now we pull that message off and acknowledge it.
	 */
	channel_msg_t msg;
 	channel_recvmsg(&ch, &msg);
#endif	
	return ESUCCESS;
}

error_t channel_recvmsg(channel_t* ch, channel_msg_t* msg) {
	/*
	 * First copy the data contained in the message from shared page pointed 
	 * to by the entry in the ring buffer to the msg struct passedinto this 
	 * function
	 */
	channel_msg_t* msg_copy = RING_GET_REQUEST(&(ch->ring_side.back), 
	                                          ch->ring_side.back.req_cons++);
	msg->len = msg_copy->len; 
	memcpy(msg->buf, ch->data_addr + (size_t)(msg_copy->buf), msg->len);
	
	/*
	 * Then acknowledge that its been serviced / in the process of being 
	 * serviced.
	 */	
	channel_ack_t* ack = RING_GET_RESPONSE(&(ch->ring_side.back), 
	                                       ch->ring_side.back.rsp_prod_pvt++);	
	RING_PUSH_RESPONSES(&(ch->ring_side.back));	

	return ESUCCESS;
}
