// buggy program - causes a divide by zero exception

#include <inc/lib.h>

int zero;

int main(int argc, char** argv)
{
	cprintf("1/0 is %08x!\n", 1/zero);
	return 0;
}

