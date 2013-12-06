#include <assert.h>
#include <stdio.h>

int main()
{
	#define N 12345
	double x[N];
	double y[N];
	double z[N];

	for(int i = 0; i < N; i++)
	{
		x[i] = (double)i;
		y[i] = (double)(2*i);
	}

	for(int i = 0; i < N; i++)
		z[i] = x[i]+y[i];

	for(int i = 0; i < N; i++)
		assert((int)z[i] == 3*i);

	printf("vvadd works!\n");

	return 0;
}
