#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdio.h>
#include <assert.h>
#include <packetizer.h>
#include <stdexcept>
#include <fstream>

#ifdef DEBUG_MODE
# define debug(...) (__VA_ARGS__)
#else
# define debug(...) do { } while(0)
#endif

static __inline uint64_t
read_tsc(void)
{
    uint64_t tsc;
    __asm __volatile("rdtsc" : "=A" (tsc));
    return tsc;
}

packetizer::packetizer(const char *target_mac, const char *eth_device, 
	                     const char *filename)
{
	seqno = 0;
	memcpy(this->target_mac, target_mac, 6);
	strcpy(this->eth_device, eth_device);
	strcpy(this->filename, filename);
	memset(&myaddr, 0, sizeof(myaddr));

	// setuid root to open a raw socket.  if we fail, too bad
	seteuid(0);
	sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	seteuid(getuid());
	if(sock < 0)
	  throw std::runtime_error("socket() failed! Maybe try running as root...");

	myaddr.sll_ifindex = if_nametoindex(eth_device);
	myaddr.sll_family = AF_PACKET;

	int ret = bind(sock, (struct sockaddr *)&myaddr, sizeof(myaddr));
	if (ret < 0)
	  throw std::runtime_error("bind() failed!");

	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(struct timeval)) < 0)
	  throw std::runtime_error("setsockopt() failed!");

	// get MAC address of local ethernet device
	struct ifreq ifr;
	strcpy(ifr.ifr_name, eth_device);
	ret = ioctl(sock, SIOCGIFHWADDR, (char *)&ifr);
	if (ret < 0)
	  throw std::runtime_error("ioctl() failed!");
	memcpy(&host_mac, &ifr.ifr_ifru.ifru_hwaddr.sa_data, 6);
}

int packetizer::start()
{
	std::ifstream file(filename, std::ios::in | std::ios::binary);
	packet p(target_mac, host_mac, seqno, MAX_PAYLOAD_SIZE, NULL);

	int ret;
	printf("Starting to packetize the file: %s\n", filename);
	file.read(p.payload, MAX_PAYLOAD_SIZE);
	while(file) {
		//printf("Sending chunk %d\n", seqno);
		ret = ::sendto(sock, (char*)&p, p.size(), 0,
		               (sockaddr*)&myaddr,sizeof(myaddr));
		
		volatile uint64_t tsc = read_tsc();
		while((read_tsc() - tsc) < 34800);
		if (ret < 0)
		  throw std::runtime_error("sending packet failed!");
		p.header.seqno = htons(next_seqno());
		file.read(p.payload, MAX_PAYLOAD_SIZE);
	}
	if(file.gcount()) {
		p.header.payload_size = ntohl(file.gcount());
		p.packet_size = sizeof(p.header)+file.gcount();
		
		ret = ::sendto(sock, (char*)&p, p.size(), 0,
		               (sockaddr*)&myaddr,sizeof(myaddr));
		if (ret < 0)
		  throw std::runtime_error("sending packet failed!");
		//printf("Sending chunk %d\n", seqno);
	}
	printf("Last chunk had %u bytes...\n", file.gcount());
}

int main(int argc, char** argv)
{
	char target_mac[6];
	char eth_device[256];
	char filename[256];
	if(argc == 1) {
		target_mac[0] = 0x00;
		target_mac[1] = 0x24;
		target_mac[2] = 0x1d;
		target_mac[3] = 0x10;
		target_mac[4] = 0xa2;
		target_mac[5] = 0xb5;
		strcpy(eth_device, "eth0");
		strcpy(filename, "../../fs/i686/tests/e.y4m");
	}
	if(argc > 1) {
		assert(argc == 4);
		assert(strlen(argv[1]) == 17);
		sscanf(argv[1], "%2x:%2x:%2x:%2x:%2x:%2x", (unsigned int *)&target_mac[0],
		                                           (unsigned int *)&target_mac[1],
		                                           (unsigned int *)&target_mac[2],
		                                           (unsigned int *)&target_mac[3],
		                                           (unsigned int *)&target_mac[4],
		                                           (unsigned int *)&target_mac[5]);
		strcpy(eth_device, argv[2]);
		strcpy(filename, argv[3]);
		
	}
	packetizer p(target_mac, eth_device, filename);
	p.start();
	return 0;
}

