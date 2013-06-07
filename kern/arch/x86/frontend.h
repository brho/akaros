/*
 * Copyright (c) 2010 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_ARCH_FRONTEND_H
#define ROS_ARCH_FRONTEND_H

#define APPSERVER_MAX_PAYLOAD_SIZE 1024

#define APPSERVER_CMD_LOAD  0
#define APPSERVER_CMD_STORE 1
#define APPSERVER_CMD_ACK   2

int handle_appserver_packet(const char *buf, size_t len);

typedef struct
{
	uint8_t dst_mac[6];
	uint8_t src_mac[6];
	uint16_t ethertype;
	uint8_t cmd;
	uint8_t seqno;
	uint32_t payload_size;
	uint32_t addr;
} appserver_packet_header_t;

typedef struct
{
	appserver_packet_header_t header;
	uint8_t payload[APPSERVER_MAX_PAYLOAD_SIZE];
} appserver_packet_t;

#endif
