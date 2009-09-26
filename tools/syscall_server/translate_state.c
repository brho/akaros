#include <sys/stat.h>
#include <stdio.h>
#include <newlib_stat.h>
#include "syscall_server.h"

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
void translate_flags(int native, int newlib) {
}
void translate_mode(int native, int newlib) {
}
void translate_dir(int native, int newlib) {
}
