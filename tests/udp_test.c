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
		memcpy(&server.sin_addr.s_addr, host->h_addr, host->h_length);
	
	char* printbuf = (char*)&server.sin_addr.s_addr;
	int size = sizeof(server.sin_addr.s_addr);	
	int i;
	for (i=0; i<size;i++) {
		printf("%x", ((char*)printbuf)[i]); 
	}

	//server.sin_addr = *((struct in_addr *)host->h_addr);
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);	
	if (sockfd==-1) {
		printf("socket error\n");
		return -1;
	}

	printf ("udp_test: sockfd %d \n", sockfd);
	int socklen = sizeof(server);
	// sending large chunk of data of 2K, more than one frame
	// int sendsize =  sendto(sockfd, bulkdata, LARGE_BUFFER_SIZE, 0, (struct sockaddr*) &server, socklen);

	// sending a large chunk of data but fitting in one packet
	//int sendsize =  sendto(sockfd, bulkdata, 500, 0, (struct sockaddr*) &server, socklen);
	fd_set readset;
	int sendsize = sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr*) &server, socklen);
	printf("sendto returns %d, errno %d\n", sendsize, errno);
	//assume BUF_SIZE is larger than the packet.. so we will get to see what actually comes back..
	int j=0;
	int result;
	for (j=0; j<10; j++){
		strcpy(recv_buf, "DEADBEEFDEADBEE");
		// select before a blocking receive
		do {
			FD_ZERO(&readset);
			FD_SET(sockfd, &readset);
			result = select(sockfd + 1, &readset, NULL, NULL, NULL);
			printf("select result %d \n", result);
			printf("readset %d \n", FD_ISSET(sockfd, &readset));
		} while (result == -1 && errno == EINTR);
		// configure recvfrom not to block when there is 

		if (((n = recvfrom(sockfd, recv_buf, 5, 0, (struct sockaddr*) &server, &socklen))< 0)){ // should discard if it is udp..
			printf("recv failed\n");
		}
		recv_buf[n-1] = 0; //null terminate
		printf("[OUTPUT] recv %d with length %d from result %s\n", j,n,  recv_buf);
	}
	while(1){;}
	close(sockfd);
}
