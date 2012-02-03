#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <parlib.h>
#include <timing.h>
	
int main(void)
{
	int pFile, *first;
	pid_t pid;
	pFile = open ("hello.txt", O_RDWR | O_CREAT, (mode_t)0600);
	/* this mmap will give you a Bus Error on linux if you try to map more
	 * pages than the file contains (i think)... */
	first = (int*)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, pFile, 0);
	if (first == MAP_FAILED) {
		fprintf(stderr, "Unable to mmap the file (%d), aborting!\n", errno);
		return -1;
	}
	first[0] = 3;
	printf("the first number after initialization is %d at %08p\n", first[0],
	       first);
	if ((pid = fork()) < 0) {
		perror("fork error");
		exit(1);
	}
	if (pid == 0) {
		/* delay here, to avoid the race a bit */
		udelay(1000000);
		printf("After fork in the parent, the first number is %d\n", first[0]);
		first[0] = 99;
		printf("Pid 0 sees value %d at mmapped address %08p\n", first[0],
		       first);
	} else {
		printf("After fork in the child, the first number is %d\n", first[0]);
		first[0] = 11;
		printf("Child pid %d sees value %d at mmapped address %08p\n", pid,
		       first[0], first);
	}
}
