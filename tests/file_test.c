#include <rstdio.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

int main() 
{ 
	FILE *file; 
	file = fopen("/dir1/f1.txt","w+b");
	if (file == NULL)
		printf ("Failed to open file \n");
	fprintf(file,"%s","hello, world\n"); 
	fclose(file); 

	int fd = open("../../..//////dir1/test.txt", O_RDWR | O_CREAT );
	char rbuf[256] = {0}, wbuf[256] = {0};
	int retval;
	retval = read(fd, rbuf, 16);
	printf("Tried to read, got %d bytes of buf: %s\n", retval, rbuf);
	strcpy(wbuf, "paul <3's the new 61c");
	retval = write(fd, wbuf, 22);
	printf("Tried to write, wrote %d bytes\n", retval);
	printf("Trying to seek to 0\n");
	lseek(fd, 0, SEEK_SET);
	retval = read(fd, rbuf, 64);
	printf("Tried to read again, got %d bytes of buf: %s\n", retval, rbuf);

	retval = access("/bin/laden", X_OK);
	if (errno != ENOENT)
		printf("WARNING! Access error for Osama!\n");
	retval = access("////../../////dir1/f1.txt", R_OK);
	if (retval < 0)
		printf("WARNING! Access error for f1.txt!\n");

	struct stat st = {0};
	//retval = stat("/bin/mhello", &st);
	retval = fstat(fd, &st);
	printf("Tried to stat, was told %d\n", retval);
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

	retval = symlink("/dir1/random.txt", "/dir2/sym-test");
	if (retval < 0)
		printf("WARNING! Symlink creation failed!\n");
	retval = readlink("/dir2/sym-test", rbuf, 256);
	if (retval < 0)
		printf("WARNING! Readlink failed!\n");
	else
		printf("Readlink read %d bytes\n", retval);
	
	/* Readdir tests: two ways to do it: */
	DIR *dir = opendir("/dir1/");
	struct dirent dirent_r, *dirent, *result = 0;
	#if 0
	dirent = readdir(dir);
	printf("Readdir: d_ino %lld, d_off: %lld, d_reclen: %d, d_name: %s\n",
	       dirent->d_ino, dirent->d_off, dirent->d_reclen, dirent->d_name);
	printf("TAKE TWO:\n-----------\n");
	dirent = readdir(dir);
	printf("Readdir: d_ino %lld, d_off: %lld, d_reclen: %d, d_name: %s\n",
	       dirent->d_ino, dirent->d_off, dirent->d_reclen, dirent->d_name);
	#endif

	retval = readdir_r(dir, &dirent_r, &result);
	if (retval > 0)
		printf("WARNING! Readdir_r failed!, retval %d\n", retval);
	if (!result)
		printf("End of the directory\n");
	else
		printf("Dirent name: %s\n", result->d_name);
	printf("TAKE TWO:\n-----------\n");
	memset(&dirent_r, 0, sizeof(struct dirent));
	retval = readdir_r(dir, &dirent_r, &result);
	if (retval > 0)
		printf("WARNING! Readdir_r failed!, retval %d\n", retval);
	if (!result)
		printf("End of the directory\n");
	else
		printf("Dirent name: %s\n", result->d_name);

	breakpoint();
}
