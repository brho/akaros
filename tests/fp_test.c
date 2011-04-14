#include <stdio.h>

int main(int argc, char** argv) 
{
	volatile float x = 2.5;
	volatile float y = 5.0;
	volatile float var = x*y;
	printf("value decimal: %d\n", (int)var);
//	printf("value floating: %f\n", var);

//	int x = 25;
//	int y = 50;
//	int var = x*y;
//	printf("value: %d\n", var);
} 
