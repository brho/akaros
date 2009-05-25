// buggy program - faults with a write to a kernel location

#include <inc/lib.h>

int main(int argc, char** argv)
{ TRUSTEDBLOCK
	*(unsigned*)0xf0100000 = 0;
	return 0;
}

