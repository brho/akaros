// buggy program - faults with a read from location zero

#include <inc/lib.h>

int main(int argc, char** argv)
{ TRUSTEDBLOCK
	cprintf("I read %08x from location 0!\n", *(unsigned*)0);
	return 0;
}

