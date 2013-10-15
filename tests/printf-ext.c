/* Basic tests for custom printf strings, such as %i (ipaddr) and %r */

#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <printf-ext.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

uint8_t v4addr[] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0xff, 0xff,
	192, 168, 0, 2
};

uint8_t v6addr[] = {
	0xfe, 0x80, 0, 0,
	0, 0, 0, 0,
	0x20, 0x0, 0x0a, 0xff,
	0xfe, 0xa7, 0x0f, 0x7c
};

uint8_t v6mask[] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	255, 255, 192, 0
};

uint8_t ethaddr[] = {
	0x90, 0xe6, 0xba, 0x12, 0x48, 0x64
};

int main(int argc, char **argv)
{
	int ret;
	if (register_printf_specifier('i', printf_ipaddr, printf_ipaddr_info))
		printf("Failed to register 'i'\n");
	if (register_printf_specifier('M', printf_ipmask, printf_ipmask_info))
		printf("Failed to register 'M'\n");
	if (register_printf_specifier('E', printf_ethaddr, printf_ethaddr_info))
		printf("Failed to register 'E'\n");

	printf("IPv4 addr %i\n", v4addr);
	printf("IPv6 addr %i\n", v6addr);
	printf("IPv6 mask %M\n", v6mask);
	printf("IPv4 addr as mask %M\n", v4addr);
	printf("IPv6 addr as mask %M\n", v6addr);
	printf("ethaddr %E\n", ethaddr);
	printf("ethaddr null %E\n", 0);

	ret = open("/9/proc/no/such/file", 0, 0);
	printf("Open ret %d, errstr: %r\n", ret);
	printf("%s %i %M %d %s %r %E\n", "testing a few,", v6addr, v6mask, 1337,
	       "more cowbell", ethaddr);

	printf("Done\n");
	return 0;
}
