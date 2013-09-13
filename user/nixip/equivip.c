#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include "ip.h"

int
equivip4(uint8_t *a, uint8_t *b)
{
	int i;

	for(i = 0; i < 4; i++)
		if(a[i] != b[i])
			return 0;
	return 1;
}

int
equivip6(uint8_t *a, uint8_t *b)
{
	int i;

	for(i = 0; i < IPaddrlen; i++)
		if(a[i] != b[i])
			return 0;
	return 1;
}
