#include <stdio.h>
#include <parlib.h>

int main(int argc, char** argv)
{
	printf("[Scottish Accent]: She's goin' down, Cap'n!\n");
	sys_reboot();
	printf("Doh!  Reboot returned...\n");
	return -1;
}
