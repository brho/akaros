
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
#include <sys/time.h>

int main(int argc, char *argv[]) 
{ 
	int fd;
	int i;
	static uint32_t data[1048576];
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	int usecs;
	int amt = 4096;

	if (argc > 1)
		amt = strtol(argv[1], 0, 0);
	if (amt > sizeof(data))
		printf("max amt to read is %d\n", sizeof(data));
	if (amt > sizeof(data))
		amt = sizeof(data);
	fd = open("#c/random", 0);
	if (fd < 0){
		perror("random");
		exit(1);
	}
	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");
	amt = read(fd, data, amt);
	if (amt < 0){
		perror("read random");
		exit(1);
	}
	if (gettimeofday(&end_tv, 0))
		perror("End time error...");
	usecs = (end_tv.tv_sec - start_tv.tv_sec)*1000000 + (end_tv.tv_usec - start_tv.tv_usec);
	printf("Read %d bytes of random in %d microseconds\n", amt, usecs);
	for(i = 0; i < 4; i++)
		printf("%08x ", data[i]);
	printf("\n");

}
