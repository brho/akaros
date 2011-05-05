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
#include <ros/procdata.h>
#include <ros/resource.h>
#include <stdint.h>
#include <ros_debug.h>

enum {
	PG_RDONLY = 4,
	PG_RDWR   = 6,
};

ssize_t     sys_cputs(const uint8_t *s, size_t len);
uint16_t    sys_cgetc(void);
int         sys_null(void);
ssize_t     sys_serial_write(void* buf, size_t len); 
ssize_t     sys_serial_read(void* buf, size_t len);
ssize_t     sys_eth_write(void *COUNT(len) buf, size_t len); 
ssize_t     sys_eth_read(void *COUNT(len) buf, size_t len);
size_t      sys_getcpuid(void);
/* Process Management */
int         sys_getpid(void);
int         sys_proc_destroy(int pid, int exitcode);
void        sys_yield(bool being_nice);
int         sys_proc_create(char *path, size_t path_l, char *argv[], char *envp[]);
int         sys_proc_run(int pid);
ssize_t     sys_shared_page_alloc(void *COUNT(PGSIZE) *addr, pid_t p2, 
                                  int p1_flags, int p2_flags);
ssize_t     sys_shared_page_free(void *COUNT(PGSIZE) addr, pid_t p2);
ssize_t     sys_resource_req(int type, size_t amt_max, size_t amt_min, uint32_t flags);
void        sys_reboot();
int         gettimeofday(struct timeval* tp, void* tzp);
void *COUNT(length) sys_mmap(void *SNT addr, size_t length, int prot, int flags,
                             int fd, size_t offset);
int         sys_notify(int pid, unsigned int ev_type, struct event_msg *u_msg);
int         sys_self_notify(uint32_t vcoreid, unsigned int ev_type,
                            struct event_msg *u_msg);
int         sys_halt_core(unsigned int usec);
void*		sys_init_arsc();
int         sys_block(unsigned int usec);

#endif	// !ASSEMBLER

#endif	// !ROS_INC_PARLIB_H
