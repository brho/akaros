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
//single fragment for now
#define BUF_SIZE 16

int main(int argc, char* argv[]) {
	struct sockaddr_in server;
	char buf[BUF_SIZE] = "hello world";
	int sockfd, n;
	struct  hostent* host;
	if (argc != 1) {
		printf("where is my hostname?\n");

		return -1;
	}
	// ignore the host for now
	// host = gethostbyname(argv[1]);
	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(5000);
	server.sin_addr.s_addr = inet_addr("10.0.0.1"); //hardcoded server
	//server.sin_addr = *((struct in_addr *)host->h_addr);

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) ==-1) {
		printf("socket error\n");
		return -1;
	}
	printf ("udp_test: sockfd %d \n", sockfd);

	int sendsize = sendto(sockfd, buf, BUF_SIZE, 0, (struct sockaddr*) &server, sizeof(server));
	printf ("sendto returns %d, errno %d\n", sendsize, errno);
/*
	if ((n = recvfrom(sockfd, buf, BUF_SIZE, 0, NULL, NULL)< 2)){
		printf ("recv failed\n");
		return -1;
	}

	buf[n-2] = 0; //null terminate
	printf("%s\n", buf);
*/	
}
