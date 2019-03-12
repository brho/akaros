#include <stdio.h>
#include <stdlib.h>

void main(int argc, char *argv[])
{
	argc--,argv++;
	while (argc > 0) {
		unsigned char *p = (void *)strtoul(argv[0], 0, 0);

		printf("%p shows %02x\n", p, *p);
		argc--,argv++;
	}
}
