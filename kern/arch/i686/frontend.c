#include <multiboot.h>
#include <arch/frontend.h>
#include <arch/nic_common.h>
#include <kmalloc.h>

#define debug(...) printk(__VA_ARGS__)

int handle_appserver_packet(const char* p, size_t size)
{
	// Subtract off the crc because we just don't care...
	size-=4;

	appserver_packet_t* packet = (appserver_packet_t*)p;

	if(size < sizeof(packet->header))
		goto fail;

	uint8_t cmd = packet->header.cmd;
	if(cmd != APPSERVER_CMD_LOAD && cmd != APPSERVER_CMD_STORE)
		goto fail;

	uintptr_t paddr = ntohl(packet->header.addr);
	size_t copy_size = ntohl(packet->header.payload_size);
	if(paddr % 4 || paddr >= maxaddrpa)
		goto fail;
	if(copy_size % 4 || copy_size > APPSERVER_MAX_PAYLOAD_SIZE)
		goto fail;

	size_t paysize = copy_size;
	size_t response_paysize = 0;
	if(cmd == APPSERVER_CMD_LOAD)
	{
		response_paysize = copy_size;
		paysize = 0;
	}
	if(size != sizeof(packet->header) + paysize &&
	   !(size == MIN_FRAME_SIZE && sizeof(packet->header) + paysize <= MIN_FRAME_SIZE))
		goto fail;

	// construct response packet
	size_t response_size = sizeof(packet->header)+response_paysize;
	appserver_packet_t* response_packet = kmalloc(response_size,0);

	memcpy(response_packet->header.dst_mac,packet->header.src_mac,6);
	memcpy(response_packet->header.src_mac,packet->header.dst_mac,6);
	response_packet->header.ethertype = packet->header.ethertype;
	response_packet->header.cmd = APPSERVER_CMD_ACK;
	response_packet->header.seqno = packet->header.seqno;
	response_packet->header.payload_size = htonl(response_paysize);
	response_packet->header.addr = packet->header.addr;
	
	// determine src/dest for copy
	const uint32_t* copy_src = (const uint32_t*)packet->payload;
	uint32_t* copy_dst = (uint32_t*)KADDR(paddr);
	if(cmd == APPSERVER_CMD_LOAD)
	{
		copy_src = copy_dst;
		copy_dst = (uint32_t*)response_packet->payload;
	}

	// manual word-by-word copy for word-atomicity
	for(int i = 0; i < copy_size/sizeof(uint32_t); i++)
		copy_dst[i] = copy_src[i];

	// fire the response
	if(send_frame((char*)response_packet,response_size) != response_size)
		panic("couldn't send appserver packet!");
	kfree(response_packet);

	return 0;
	
fail:
	panic("bad appserver packet!");
}
