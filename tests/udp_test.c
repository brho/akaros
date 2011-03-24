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

/* Test program
 *
 * Pings the server at argv1: argv2
 * gets a response and prints it
 */

int main(int argc, char* argv[]) {
	struct sockaddr_in server;
	char buf[BUF_SIZE] = "hello world";
	char recv_buf[BUF_SIZE];
	int sockfd, n;
	struct hostent* host;
	if (argc != 3){
		printf("udp_test hostname portnum\n");
		return -1;
	}
	// ignore the host for now
	//host = gethostbyname(argv[1]); //hostname
	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(atoi(argv[2]));
	server.sin_addr.s_addr = inet_addr("10.0.0.1"); //hardcoded server 
	
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

	int sendsize = sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr*) &server, socklen);
	printf("sendto returns %d, errno %d\n", sendsize, errno);
	//assume BUF_SIZE is larger than the packet.. so we will get to see what actually comes back..
	int j=0;
	for (j=0; j<1; j++){
		strcpy(recv_buf, "DEADBEEFDEADBEE");
		if (((n = recvfrom(sockfd, recv_buf, BUF_SIZE, 0, (struct sockaddr*) &server, &socklen))< 0)){
			printf("recv failed\n");
		}
		recv_buf[n-2] = 0; //null terminate
		printf("recv %d with length %d from result %s\n", j,n,  recv_buf);
	}


	close(sockfd);
}
