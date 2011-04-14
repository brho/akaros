#include <stdio.h>
#include <parlib.h>
#include <arch/arch.h>

int main()
{
	int N = 8192;

	long long tsc0 = read_tsc();
	for(int i = 0; i < N; i++)
		ros_syscall(SYS_null, 0, 0, 0, 0, 0, 0);
	long long tsc1 = read_tsc();

	printf("tsc0 = %lld\n",tsc0);
	printf("syscall time = %lld\n",(tsc1-tsc0)/N);

	return 0;
}
