/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */
 
#ifndef ROS_CHANNEL_H
#define ROS_CHANNEL_H
 
#include <stdint.h>
#include <arch/types.h>
#include <ros/error.h>
#include <ros/env.h>
#include <ros/ring_buffer.h>

/***************** Channel related constants *****************/

enum {
	CHANNEL_POLL   = 0,
//	CHANNEL_NOTIFY = 1,
};

/*************************************************************/

/***************** Channel related structures *****************/

typedef struct channel_msg {
	size_t len;
	void* buf;
} channel_msg_t;

typedef struct channel_ack {
	int32_t ack;
} channel_ack_t;

// Generic Syscall Ring Buffer
DEFINE_RING_TYPES(channel, channel_msg_t, channel_ack_t);

typedef enum {
	CHANNEL_CLIENT = 1,
	CHANNEL_SERVER = 2,
} channel_type_t;

typedef struct channel {
	size_t id;
	channel_type_t type;
	envid_t endpoint;
	void *COUNT(PGSIZE) data_addr;
	channel_sring_t *COUNT(1) ring_addr;
	union {
		channel_front_ring_t front WHEN(type == CHANNEL_CLIENT);
		channel_back_ring_t back WHEN(type == CHANNEL_SERVER);
	} ring_side;
} channel_t;

typedef struct channel_attr {
} channel_attr_t;

/*************************************************************/

/***************** Channel related functions *****************/

error_t channel_create(envid_t server, channel_t* ch, channel_attr_t* attr);
error_t channel_create_wait(channel_t* ch, channel_attr_t* attr);
error_t channel_destroy(channel_t* ch);

error_t channel_sendmsg(channel_t* ch, channel_msg_t* msg);
error_t channel_recvmsg(channel_t* ch, channel_msg_t* msg);

/*************************************************************/


#endif //ROS_CHANNEL_H








