#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <newlib_backend.h>

int main(int argc, char** argv)
{
	printf("Hello world from newlib!!\n");
	int fd = open("/test/file", O_RDWR, 0);
	char buf[10];
	int read_amt = read(fd, buf, 10);
	printf("FD: %d\nRead Amount: %d\n", fd, read_amt);
	return 0;
}
