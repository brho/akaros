#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <parlib/arch/arch.h>

int main(int argc, char *argv[])
{
	uint32_t eax, ebx, ecx, edx;
	uint32_t info1, info2 = 0;

	if (argc < 2) {
		printf("%s: eax_leaf [ecx_leaf]\n", argv[0]);
		exit(-1);
	}
	errno = 0;
	info1 = strtoul(argv[1], 0, 16);
	if (errno) {
		perror("info1");
		exit(-1);
	}
	if (argc > 2) {
		errno = 0;
		info2 = strtoul(argv[2], 0, 16);
		if (errno) {
			perror("info2");
			exit(-1);
		}
	}
	eax = ebx = ecx = edx = 0xffffffff;
	parlib_cpuid(info1, info2, &eax, &ebx, &ecx, &edx);
	printf("CPUID for Leaf 0x%08x, Sublevel 0x%08x:\n", info1, info2);
	printf("\teax: %08x\n\tebx: %08x\n\tecx: %08x\n\tedx: %08x\n", eax, ebx,
	       ecx, edx);
	return 0;
}
