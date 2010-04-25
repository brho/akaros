#ifndef _PACKETIZER_H
#define _PACKETIZER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netpacket/packet.h>

#define PACKETIZER_ETHERTYPE 0xabcd
#define MAX_PAYLOAD_SIZE 1024
#define MAX_PACKET_SIZE (MAX_PAYLOAD_SIZE+sizeof(packet_header))
struct packet_header
{
	uint8_t dst_mac[6];
	uint8_t src_mac[6];
	uint16_t ethertype;
	uint16_t seqno;
	uint32_t payload_size;
};

struct packet
{
	packet_header header;
	char payload[MAX_PAYLOAD_SIZE];
	uint32_t packet_size;

	uint32_t size()
	{
	  return packet_size;
	}

	packet() {}
	packet(const char* dst_mac, const char* src_mac, 
	       char seqno,int payload_size, const uint8_t* bytes)
	{
	  header.ethertype = htons(PACKETIZER_ETHERTYPE);
	  memcpy(header.dst_mac,dst_mac,6);
	  memcpy(header.src_mac,src_mac,6);
	  header.seqno = htons(seqno);
	  header.payload_size = ntohl(payload_size);
	  if(bytes)
	    memcpy(payload,bytes,payload_size);
	  packet_size = sizeof(header)+payload_size;
	}
};

class packetizer
{
public:
	
	packetizer(const char *target_mac, const char *eth_device, 
	           const char *filename);
	int start(void); 

protected:

	sockaddr_ll myaddr;
	int sock;
	char host_mac[6];
	char target_mac[6];
	char eth_device[64];
	char filename[256];

	void send_packet(packet* packet);

	uint16_t seqno;
	uint16_t next_seqno() { return seqno++; }
};

#endif // _PACKETIZER_H
