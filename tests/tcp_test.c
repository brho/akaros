
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#define BUF_SIZE 16
#define LARGE_BUFFER_SIZE 2048

/* Test program
 *
 * Pings the server at argv1: argv2
 * gets a response and prints it
 * Running in Qemu 
 * tcp_test server_port
 * tcp_test server_addr server_port
 */

int main(int argc, char* argv[]) {
	struct sockaddr_in server;
	char buf[BUF_SIZE] = "hello world";
	char bulkdata[LARGE_BUFFER_SIZE] = "testme";
	char recv_buf[BUF_SIZE];
	int sockfd, n, inqemu;
	struct hostent* host;

	// ignore the host for now
	if (argc == 2){
		printf("in qemu client\n");
		inqemu = 1;
	}
	else if (argc == 3){
		printf("linux client\n");
		inqemu = 0;
	} else 
	{
		printf("incorrect number of parameters\n");
	}
	if (!inqemu){
		host = gethostbyname(argv[1]); //hostname
	}
	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	if (inqemu)
		server.sin_port = htons(atoi(argv[1]));
	else
		server.sin_port = htons(atoi(argv[2]));


	if (inqemu)
		server.sin_addr.s_addr = inet_addr("10.0.0.1"); //hardcoded server 
	else
		// linux client, use the argument as the server address
		memcpy(&server.sin_addr.s_addr, host->h_addr, host->h_length);
	
	char* printbuf = (char*)&server.sin_addr.s_addr;
	int size = sizeof(server.sin_addr.s_addr);	
	int i;
	for (i=0; i<size;i++) {
		printf("connecting to %x \n", ((char*)printbuf)[i]); 
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);	
	if (sockfd==-1) {
		printf("socket error\n");
		return -1;
	}

	printf ("tcp_test: sockfd %d \n", sockfd);
	int socklen = sizeof(server);

	// Set up a connection with the server
	int rc;

	if((rc = connect(sockfd, (struct sockaddr *)&server, sizeof(server))) < 0)
	{
		// testing closing a socket file
		close(sockfd);
		exit(-1);
	}
	// sending large chunk of data of 2K, more than one frame
	// int sendsize =  sendto(sockfd, bulkdata, LARGE_BUFFER_SIZE, 0, (struct sockaddr*) &server, socklen);

	fd_set readset;
	int sendsize = send(sockfd, buf, strlen(buf), 0);
	if (sendsize != strlen(buf)) 
		printf("send operation failed error code %d \n", sendsize);
	int j=0;
	int result;
	for (j=0; j<10; j++){
		strcpy(recv_buf, "DEADBEEFDEADBEE");
		// in udp_test, we can recv_from with a size of 5, because it discards the rest of the packet. In the TCP
		// version, we need to be precise about the size of the recv, because the left over data will be displayed in the next
		// packet.
		if ((n = recv(sockfd, recv_buf, 14 , 0))< 0){ 
			printf("recv failed\n");
		}
		// recv_buf[n-1] = 0; //null terminate
		printf("[OUTPUT] recv %d with length %d from result %s\n", j,n,  recv_buf);
	}
	while(1){;}
	close(sockfd);
}
