#include <stdlib.h>
#include <rstdio.h>
#include <arch/arch.h>

int main(int argc, char** argv)
{
	printf("[T]:004:M:%llu\n", read_tsc());
	return 0;
}
