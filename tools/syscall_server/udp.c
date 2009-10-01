#include <fcntl.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define UDP_ARR_SIZE 4096

int port = 44444;
char udp_arr[UDP_ARR_SIZE];
int udp_buf_len = 0;
int udp_cur_pos = 0;
struct sockaddr_in ret_addr;


int init_syscall_server(int* fd_read, int* fd_write) {

	int listen_socket;

	// Address structures
	struct sockaddr_in server_addr;

	// Size of our address structure
	unsigned int addr_len = sizeof(struct sockaddr_in);

	if ((listen_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		printf("Error: PORT: %i. Could not create socket.\n", port);
		exit(1);
	}

	// Setup sockaddr_in
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr=INADDR_ANY;
	bzero(&(server_addr.sin_zero), 8);  	// This is apparently a source of bugs? Who knew.

	// Bind to the given port.
	if (bind(listen_socket, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)))
	{
		printf("Error: Spawned Process, PORT: %i. Could not bind to port.\n", port);
		exit(1);
	}

	struct hostent *hp;
	hp = gethostbyname("192.168.0.10");
	memset(&ret_addr, 0, sizeof(ret_addr));
	ret_addr.sin_family = AF_INET;
	memcpy(&ret_addr.sin_addr, hp->h_addr, hp->h_length);
	ret_addr.sin_port = htons(44443);

	*fd_read = listen_socket;
	*fd_write = listen_socket;
	
	return listen_socket + listen_socket;

}

int read_syscall_server(int fd, char* buf, int len) {

	if (udp_buf_len == 0) {
		udp_buf_len = recvfrom(fd, udp_arr, UDP_ARR_SIZE, 0, 0, 0);

		if (udp_buf_len== 0)
			return -1;
	}

	if ((udp_cur_pos + len) > udp_buf_len)
		return -1;

	memcpy(buf, udp_arr + udp_cur_pos, len);

	udp_cur_pos = udp_cur_pos + len;

	if (udp_cur_pos == udp_buf_len) {
		udp_cur_pos = 0;
		udp_buf_len = 0;
	}

	return len;

}

int write_syscall_server(int fd, char* buf, int len, int bytes_to_follow) {

	static int bytes_buffered = 0;
	static char* internal_buffer = NULL;
	static int buffer_size = 0;
	
	if (bytes_to_follow != 0) {

		if (internal_buffer != NULL) {
			printf("Called buffered write after a buffered write. Illegal.\n");
			exit(1);
		}

		internal_buffer = malloc(len + bytes_to_follow);

		if (internal_buffer == NULL) {
			printf("Could not malloc send buffer.\n");
			exit(1);
		}

		buffer_size = len + bytes_to_follow;

		memcpy(internal_buffer, buf, len);

		bytes_buffered = len;

		return bytes_buffered;

	} else if (bytes_buffered == 0) {
		return sendto(fd, buf, len, 0, (struct sockaddr *)&ret_addr, sizeof(ret_addr));
	} else {

		if ((len + bytes_buffered) != buffer_size) {
			printf("Buffered size does not match actual size\n");
			exit(1);
		}

		if (internal_buffer == NULL) {
			printf("Bytes bufferd out of sync with buffer\n");
			exit(1);
		}

		memcpy(internal_buffer + bytes_buffered, buf, len);

		int ret_val = sendto(fd, internal_buffer, len + bytes_buffered, 0, 
				     (struct sockaddr *)&ret_addr, sizeof(ret_addr));
		
		free(internal_buffer);
		internal_buffer = NULL;
		bytes_buffered = 0;
		buffer_size = 0;

		return ret_val;

	}
}
