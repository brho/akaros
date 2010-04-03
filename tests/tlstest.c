#include <stdio.h>

volatile __thread int foo;
volatile __thread int bar;

int main()
{
	printf("&foo = %p, &bar = %p\n",&foo,&bar);
	bar = 0xcafebabe;
	printf("bar = %p\n",bar);
	return 0;
}
