
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
	int ret;
	int flag;
	int netfd;
	int netctl;
	char name[32];
	char path[128];
	netctl = open("#l/ether0/clone", 0);
	if (netctl < 0)
		perror("netctl");
	memset(name, 0, sizeof(name));
	if (read(netctl, name, sizeof(name)) < 1)
		perror("read name");
	sprintf(path, "#l/ether0/%s/data", name);
	netfd = open(path, O_RDWR);
	if (write(netfd, path, sizeof(path)) < sizeof(path))
		perror("write to net");

}
