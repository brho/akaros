#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv)
{
	printf("Hello world from newlib!!\n");
	printf("Hello\nWorld\nMulti\nLine\n");
	printf("Hello after multiline.\n");

	int fd = open("/test/file", O_RDWR, 0);
	char buf[10];
	int read_amt = read(fd, buf, 10);
	printf("FD: %d\n", fd);
	printf("read_amt: %d\n", read_amt);
	printf("read: %s\n", buf);

	char buf2[] = "NANWAN!\n";

	int write_amt = write(fd, buf2, 8);
        printf("write_amt: %d\n", write_amt);        
	printf("wrote: %s\n", buf2);


	return 0;
}
