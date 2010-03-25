#ifndef ROS_INCLUDE_SYSCALL_H
#define ROS_INCLUDE_SYSCALL_H

/* system call numbers.  need to #def them for use in assembly. */
#define SYS_null					 1
#define SYS_cache_buster			 2
#define SYS_cache_invalidate		 3
#define SYS_reboot					 4
#define SYS_cputs					 5
#define SYS_cgetc					 6
#define SYS_getcpuid				 7
#define SYS_getvcoreid				 8
#define SYS_getpid					 9
#define SYS_proc_create				10
#define SYS_proc_run				11
#define SYS_proc_destroy			12
#define SYS_yield					13
#define SYS_run_binary				14
#define SYS_fork					15
#define SYS_exec					16
#define SYS_trywait					17
#define SYS_mmap					18
#define SYS_munmap					19
#define SYS_mprotect				20
/* // these are the other mmap related calls, some of which we'll implement
#define SYS_mincore // can read page tables instead
#define SYS_madvise
#define SYS_mlock
#define SYS_msync
*/
#define SYS_brk						21
#define SYS_shared_page_alloc		22
#define SYS_shared_page_free		23
#define SYS_resource_req			24
/* Platform specific syscalls */
#define SYS_serial_read				25
#define SYS_serial_write			26
#define SYS_eth_read				27
#define SYS_eth_write				28
#define SYS_eth_get_mac_addr			29
#define SYS_eth_recv_check			30

#define SYS_read				100
#define SYS_write				101
#define SYS_open				102
#define SYS_close				103
#define SYS_fstat				104
#define SYS_stat				105
#define SYS_lstat				106
#define SYS_fcntl				107
#define SYS_access				108
#define SYS_umask				109
#define SYS_chmod				110
#define SYS_lseek				111
#define SYS_link				112
#define SYS_unlink				113
#define SYS_chdir				114
#define SYS_getcwd				115
#define SYS_gettimeofday		116
#define SYS_tcgetattr			117
#define SYS_tcsetattr			118

/* For Buster Measurement Flags */
#define BUSTER_SHARED			0x0001
#define BUSTER_STRIDED			0x0002
#define BUSTER_LOCKED			0x0004
#define BUSTER_PRINT_TICKS		0x0008
#define BUSTER_JUST_LOCKS		0x0010 // unimplemented

// for system calls that pass filenames
#define MAX_PATH_LEN 256

// convenience wrapper for __ros_syscall
#define ros_syscall(which,a0,a1,a2,a3,a4) \
   __ros_syscall(which,(long)(a0),(long)(a1),(long)(a2),(long)(a3),(long)(a4))
#include <ros/arch/syscall.h>

#endif /* !ROS_INCLUDE_SYSCALL_H */
