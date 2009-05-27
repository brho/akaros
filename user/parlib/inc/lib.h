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
#include <ros/env.h>
#include <ros/error.h>
#include <newlib_backend.h>

extern volatile env_t *env;
// will need to change these types when we have real structs
// seems like they need to be either arrays [] or functions () for it to work
extern volatile uint8_t (COUNT(PGSIZE * UINFO_PAGES) procinfo)[];
extern volatile uint8_t (COUNT(PGSIZE * UDATA_PAGES) procdata)[];

error_t 	sys_serial_write(void* buf, uint16_t len); 
uint16_t	sys_serial_read(void* buf, uint16_t len);
envid_t		sys_getenvid(void);
uint32_t	sys_getcpuid(void);
int			sys_env_destroy(envid_t);

#endif	// !ROS_INC_LIB_H
