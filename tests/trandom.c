
#define _LARGEFILE64_SOURCE /* needed to use lseek64 */

#include <stdio.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>

int main(int argc, char *argv[]) 
{ 
	int fd;
	int i;
	uint32_t data;
	fd = open("#c/random", 0);
	if (fd < 0)
		perror("random");
	for(i = 0; i < 16; i++){
		read(fd, &data, sizeof(data));
		printf("%d %08x\n", i, data);
	}
}
