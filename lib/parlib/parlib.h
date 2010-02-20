// Main public header file for our user-land support library,
// whose code lives in the lib directory.
// This library is roughly our OS's version of a standard C library,
// and is intended to be linked into all user-mode applications
// (NOT the kernel or boot loader).

#ifndef ROS_INC_PARLIB_H
#define ROS_INC_PARLIB_H 1

#ifndef __ASSEMBLER__

#include <ros/common.h>
#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/procinfo.h>
#include <stdint.h>

enum {
	PG_RDONLY = 4,
	PG_RDWR   = 6,
};

ssize_t     sys_cputs(const uint8_t *s, size_t len);
uint16_t    sys_cgetc(void);
ssize_t     sys_serial_write(void* buf, size_t len); 
ssize_t     sys_serial_read(void* buf, size_t len);
ssize_t     sys_eth_write(void *COUNT(len) buf, size_t len); 
ssize_t     sys_eth_read(void *COUNT(len) buf, size_t len);
ssize_t     sys_run_binary(void* binary_buf, size_t len,
                           const procinfo_t* pi, size_t num_colors);
int         sys_getpid(void);
size_t      sys_getcpuid(void);
void *      sys_brk(void* addr);
int     sys_proc_destroy(int pid, int exitcode);
ssize_t     sys_shared_page_alloc(void *COUNT(PGSIZE) *addr, pid_t p2, 
                                  int p1_flags, int p2_flags);
ssize_t     sys_shared_page_free(void *COUNT(PGSIZE) addr, pid_t p2);
ssize_t     sys_resource_req(int type, size_t amount, uint32_t flags);
void        sys_reboot();
void        sys_yield();
int         gettimeofday(struct timeval* tp, void* tzp);
void *COUNT(length) sys_mmap(void *SNT addr, size_t length, int prot, int flags,
                             int fd, size_t offset);

#endif	// !ASSEMBLER

#endif	// !ROS_INC_PARLIB_H
