#include <assert.h>
#include <stdio.h>

#define BAR0 0xdeadbeef
volatile __thread int foo;
volatile __thread int bar = BAR0;

int main()
{
	printf("&foo = %p, &bar = %p\n",&foo,&bar);
	assert(bar == BAR0);
	bar = 0xcafebabe;
	printf("bar = %p\n",bar);
	return 0;
}
