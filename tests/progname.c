#define __GNU_SOURCE
#include <errno.h>
#include <stdio.h>
extern char *program_invocation_name;
extern char *program_invocation_short_name;

int main(int argc, char **argv)
{
	printf("argc: %d, argv[0]: %s\n", argc, argv[0]);
	printf("program_invocation_name: %s\n", program_invocation_name);
	return 0;
}
