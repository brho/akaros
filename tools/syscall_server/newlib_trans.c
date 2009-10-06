#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <syscall_server.h>

void translate_stat(struct stat* native, struct newlib_stat* newlib) {
	newlib->st_dev = native->st_dev;
	newlib->st_ino = native->st_ino;
	newlib->st_mode = native->st_mode;
	newlib->st_nlink = native->st_nlink;
	newlib->st_uid = native->st_uid;
	newlib->st_gid = native->st_gid;
	newlib->st_rdev = native->st_rdev;
	newlib->st_size = native->st_size;
	newlib->st_atim = native->st_atim.tv_nsec;
	newlib->st_mtim = native->st_mtim.tv_nsec;
	newlib->st_ctim = native->st_ctim.tv_nsec;
	newlib->st_blksize = native->st_blksize;
	newlib->st_blocks = native->st_blocks;
}
int translate_flags(int newlib_flags) {
	int native_flags = 0;
	if (newlib_flags & NEWLIB_O_RDONLY) {
		native_flags |= O_RDONLY;
		newlib_flags &= ~NEWLIB_O_RDONLY;
	}
	if (newlib_flags & NEWLIB_O_WRONLY) {
		native_flags |= O_WRONLY;
		newlib_flags &= ~NEWLIB_O_WRONLY;
	}
	if (newlib_flags & NEWLIB_O_RDWR) {
		native_flags |= O_RDWR;
		newlib_flags &= ~NEWLIB_O_RDWR;
	}
	if (newlib_flags & NEWLIB_O_APPEND) {
		native_flags |= O_APPEND;
		newlib_flags &= ~NEWLIB_O_APPEND;
	}
	if (newlib_flags & NEWLIB_O_CREAT) {
		native_flags |= O_CREAT;
		newlib_flags &= ~NEWLIB_O_CREAT;
	}
	if (newlib_flags & NEWLIB_O_TRUNC) {
		native_flags |= O_TRUNC;
		newlib_flags &= ~NEWLIB_O_TRUNC;
	}
	if (newlib_flags & NEWLIB_O_EXCL) {
		native_flags |= O_EXCL;
		newlib_flags &= ~NEWLIB_O_EXCL;
	}
	if(newlib_flags != 0)
		fprintf(stderr, "Warning: unsupported newlib flags passed to syscall...\n");
	return native_flags;
}
int translate_mode(int newlib_mode) {
	int native_mode = 0;
	if (newlib_mode & NEWLIB_S_IRUSR) {
		native_mode |= S_IRUSR;
		newlib_mode &= ~NEWLIB_S_IRUSR;
	}
	if (newlib_mode & NEWLIB_S_IWUSR) {
		native_mode |= S_IWUSR;
		newlib_mode &= ~NEWLIB_S_IWUSR;
	}
	if (newlib_mode & NEWLIB_S_IXUSR) {
		native_mode |= S_IXUSR;
		newlib_mode &= ~NEWLIB_S_IXUSR;
	}
	if (newlib_mode & NEWLIB_S_IRGRP) {
		native_mode |= S_IRGRP;
		newlib_mode &= ~NEWLIB_S_IRGRP;
	}
	if (newlib_mode & NEWLIB_S_IWGRP) {
		native_mode |= S_IWGRP;
		newlib_mode &= ~NEWLIB_S_IWGRP;
	}
	if (newlib_mode & NEWLIB_S_IXGRP) {
		native_mode |= S_IXGRP;
		newlib_mode &= ~NEWLIB_S_IXGRP;
	}
	if (newlib_mode & NEWLIB_S_IROTH) {
		native_mode |= S_IROTH;
		newlib_mode &= ~NEWLIB_S_IROTH;
	}
	if (newlib_mode & NEWLIB_S_IWOTH) {
		native_mode |= S_IWOTH;
		newlib_mode &= ~NEWLIB_S_IWOTH;
	}
	if (newlib_mode & NEWLIB_S_IXOTH) {
		native_mode |= S_IXOTH;
		newlib_mode &= ~NEWLIB_S_IXOTH;
	}
	if(newlib_mode != 0)
		fprintf(stderr, "Warning: unsupported newlib mode passed to syscall...\n");
	return native_mode;	
}
int translate_whence(int newlib_whence) {
	int native_whence = 0;
	if (newlib_whence == NEWLIB_SEEK_SET)
		native_whence = SEEK_SET;
	else if (newlib_whence == NEWLIB_SEEK_CUR)
		native_whence = SEEK_CUR;
	else if (newlib_whence == NEWLIB_SEEK_END)
		native_whence = SEEK_END;
	return native_whence;	
}
