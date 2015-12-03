#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	int outer, inner, i, j;

	if (argc != 3) {
		fprintf(stderr, "usage: %s outer inner\n", argv[0]);
		return 1;
	}

	outer = atoi(argv[1]);
	inner = atoi(argv[2]);

	for (i = 0; i < outer; i++) {
		for (j = 0; j < inner; j++)
			printf("%c", 'x');
		fflush(stdout);
	}
	return 0;
}
