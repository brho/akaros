/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <stdlib.h>
#include <stdio.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <arpa/inet.h>

static void print_gai(struct addrinfo *ai, const char *info)
{
	struct sockaddr_in *ipv4sa;

	printf("%s: fam %d, sock %d, prot %d ", info, ai->ai_family,
	       ai->ai_socktype, ai->ai_protocol);
	char buf[128];

	ipv4sa = (struct sockaddr_in*)ai->ai_addr;
	const char *ipv4n = inet_ntop(AF_INET, &ipv4sa->sin_addr, buf, 128);

	assert(buf == ipv4n);
	printf("addr %s, port %d\n", buf, ntohs(ipv4sa->sin_port));
}

static void test_gai(const char *node, const char *serv, struct addrinfo *hints,
                     const char *info)
{
	struct addrinfo *_ai_res;
	int ret = getaddrinfo(node, serv, hints, &_ai_res);

	if (ret) {
		printf("%s: GAI failed, %d, %d %s\n", info, ret, errno,
		       errstr());
	} else {
		print_gai(_ai_res, info);
		freeaddrinfo(_ai_res);
	}
}

int main(int argc, char **argv)
{
	char name[100];
	char serv[100];

	struct addrinfo hints = {0};

	test_gai("10.0.2.1", "80", 0, "IP and 80");

	test_gai("10.0.2.1.dicks", "80", 0, "Non-number name");

	test_gai("10.0.2.2", "http", 0, "http serv");

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;
	test_gai("10.0.2.3", "12345", &hints, "SOCK_DGRAM");

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_RAW;
	hints.ai_protocol = 0;
	test_gai("10.0.2.3", "12345", &hints, "SOCK_RAW");

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = 0;
	hints.ai_protocol = IPPROTO_ICMP;
	test_gai("10.0.2.3", "12345", &hints, "ICMP");

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_TCP;
	test_gai("10.0.2.3", "12345", &hints, "Impossible hint");
}
