// buggy program - faults with a write to location zero

#include <inc/lib.h>

int main(int argc, char** argv)
{ TRUSTEDBLOCK
	*(unsigned*)0 = 0;
	return 0;
}

