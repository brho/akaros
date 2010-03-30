// test reads and writes to a large bss

#include <stdint.h>
#include <stdio.h>
#include <rassert.h>

#define ARRAYSIZE (1024*1024)

uint32_t bigarray[ARRAYSIZE];

int main(int argc, char** argv)
{
	int i;

	printf("Making sure bss works right...\n");
	for (i = 0; i < ARRAYSIZE; i++)
		if (bigarray[i] != 0)
			panic("bigarray[%d] isn't cleared!\n", i);
	for (i = 0; i < ARRAYSIZE; i++)
		bigarray[i] = i;
	for (i = 0; i < ARRAYSIZE; i++)
		if (bigarray[i] != i)
			panic("bigarray[%d] didn't hold its value!\n", i);

	printf("Yes, good.  Now doing a wild write off the end...\n");
	bigarray[ARRAYSIZE+1024] = 0;
	panic("SHOULD HAVE TRAPPED!!!");
	return 0;
}
