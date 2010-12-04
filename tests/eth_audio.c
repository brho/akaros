#include <stdio.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <timing.h>
#include <assert.h>

/* Simple test program for the audio device.  Just mmaps the stuff and reads. */
int main() 
{ 
	int in_fd, out_fd;
	void *in_buf, *out_buf;
	in_fd = open("/dev/eth_audio_in", O_RDONLY);
	out_fd = open("/dev/eth_audio_out", O_RDWR);
	assert(in_fd != -1);
	assert(out_fd != -1);
	in_buf = mmap(0, PGSIZE, PROT_READ, 0, in_fd, 0);
	if (in_buf == MAP_FAILED) {
		int err = errno;
		perror("Can't mmap the input buf:");
	}
	out_buf = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_POPULATE, out_fd, 0);
	if (out_buf == MAP_FAILED) {
		int err = errno;
		perror("Can't mmap the output buf:");
	}
	strncpy(out_buf, "Nanwan loves you!\n", 19);

	for (int i = 0; i < 20; i++) {
		udelay(5000000);
		printf("Contents: %s", in_buf);
	}

	close(in_fd);
	close(out_fd);
}
