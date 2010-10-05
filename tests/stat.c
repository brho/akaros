#include <stdio.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) 
{ 
	int retval;
	if (argc < 2) {
		printf("Prints out stats for a file\n");
		printf("Usage: stat FILENAME\n");
		return -1;
	}
	struct stat st = {0};
	retval = stat(argv[1], &st);
	if (retval < 0) {
		perror("Stat failed");
	} else {
		printf("STAT RESULTS\n---------------------\n");
		printf("dev       : %d\n", st.st_dev);
		printf("ino       : %d\n", st.st_ino);
		printf("mode      : %d\n", st.st_mode);
		printf("nlink     : %d\n", st.st_nlink);
		printf("uid       : %d\n", st.st_uid);
		printf("gid       : %d\n", st.st_gid);
		printf("rdev      : %d\n", st.st_rdev);
		printf("size      : %d\n", st.st_size);
		printf("blksize   : %d\n", st.st_blksize);
		printf("blocks    : %d\n", st.st_blocks);
		printf("atime     : %d\n", st.st_atime);
		printf("mtime     : %d\n", st.st_mtime);
		printf("ctime     : %d\n", st.st_ctime);
	}
}
