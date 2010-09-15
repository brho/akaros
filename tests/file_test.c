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
	#if 0
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
	#endif

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
	
	/* Hardlink tests */
	printf("Linking to /bin/hello at /dir1/hardhello\n");
	retval = link("/bin/hello", "/dir1/hardhello");
	if (retval < 0)
		printf("WARNING! Link failed!\n");
	//breakpoint();
	printf("Now unlinking /dir1/hardhello\n");
	retval = unlink("/dir1/hardhello");
	if (retval < 0)
		printf("WARNING! Unlink failed!\n");
	printf("Linking to /bin/hello at /bin/hardhello2\n");
	retval = link("/bin/hello", "/bin/hardhello2");
	if (retval < 0)
		printf("WARNING! Link failed!\n");
	printf("Now unlinking symlink /dir2/sym-test\n");
	retval = unlink("/dir2/sym-test");
	if (retval < 0)
		printf("WARNING! Unlink failed!\n");

	/* getcwd, on the root dir */
	char *cwd = getcwd(0, 0);
	if (!cwd)
		printf("WARNING! Couldn't get a CWD!\n");
	else
		printf("Got CWD (/): %s\n", cwd);
	free(cwd);
	/* chdir() tests */
	printf("Testing basic chdir\n");
	retval = access("dir1/f1.txt", R_OK);
	if (retval < 0)
		printf("WARNING! Access error for dir1/f1.txt!\n");
	retval = chdir("/dir1");
	if (retval < 0)
		printf("WARNING! Chdir failed for /dir1!\n");
	retval = access("f1.txt", R_OK);
	if (retval < 0)
		printf("WARNING! Access error for f1.txt!\n");
	cwd = getcwd(0, 0);
	if (!cwd)
		printf("WARNING! Couldn't get a CWD!\n");
	else
		printf("Got CWD (/dir1/): %s\n", cwd);
	free(cwd);
	/* change to a weird directory, see if we can still getcwd() */
	retval = chdir("../dir2/../dir1/dir1-1");
	if (retval < 0)
		printf("WARNING! Chdir failed for dir1-1!\n");
	cwd = getcwd(0, 0);
	if (!cwd)
		printf("WARNING! Couldn't get a CWD!\n");
	else
		printf("Got CWD (/dir1/dir1-1/): %s\n", cwd);
	free(cwd);

	/* Try a chmod() */
	printf("Trying a chmod\n");
	retval = chmod("/dir1/dir1-1/f1-1.txt", S_IRWXO);
	if (retval < 0)
		printf("WARNING! chmod failed with %d!\n", errno);

	/* Try adding a directory or two! */
	printf("Add dir3 and dir4, then remove dir4\n");
	retval = mkdir("/dir3", S_IRWXU | S_IRWXG | S_IRWXO);
	if (retval < 0)
		printf("WARNING! mkdir failed with %d!\n", errno);
	retval = mkdir("/dir4", S_IRWXU | S_IRWXG | S_IRWXO);
	if (retval < 0)
		printf("WARNING! mkdir failed with %d!\n", errno);
	retval = rmdir("/dir4");
	if (retval < 0)
		printf("WARNING! rmdir failed with %d!\n", errno);
	breakpoint();

}
