// Main public header file for our user-land support library,
// whose code lives in the lib directory.
// This library is roughly our OS's version of a standard C library,
// and is intended to be linked into all user-mode applications
// (NOT the kernel or boot loader).

#ifndef ROS_INC_LIB_H
#define ROS_INC_LIB_H 1

#include <arch/types.h>
#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/error.h>
#include <newlib_backend.h>

enum {
	PG_RDONLY = 4,
	PG_RDWR   = 6,
};

// will need to change these types when we have real structs
// seems like they need to be either arrays [] or functions () for it to work
extern volatile uint8_t (COUNT(PGSIZE * UINFO_PAGES) procinfo)[];
extern volatile uint8_t (COUNT(PGSIZE * UDATA_PAGES) procdata)[];

intreg_t syscall(uint16_t num, intreg_t a1,
                intreg_t a2, intreg_t a3,
                intreg_t a4, intreg_t a5);

ssize_t     sys_cputs(const uint8_t *s, size_t len);
uint16_t    sys_cgetc(void);
ssize_t     sys_serial_write(void* buf, size_t len); 
ssize_t     sys_serial_read(void* buf, size_t len);
int         sys_getpid(void);
size_t      sys_getcpuid(void);
error_t     sys_proc_destroy(int pid);
ssize_t     sys_shared_page_alloc(void *COUNT(PGSIZE) *addr, pid_t p2, 
                                  int p1_flags, int p2_flags);
ssize_t     sys_shared_page_free(void *COUNT(PGSIZE) addr, pid_t p2);

#endif	// !ROS_INC_LIB_H
