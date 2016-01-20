#pragma once

/* system call numbers.  need to #def them for use in assembly.  Removing
 * useless ones is okay, but if we change a number, we'll need to rebuild
 * userspace (which is why we have holes). */
#define SYS_null					 1
#define SYS_block					 2
#define SYS_cache_invalidate		 3
#define SYS_reboot					 4
#define SYS_cputs					 5
#define SYS_cgetc					 6
#define SYS_getpcoreid				 7
#define SYS_getvcoreid				 8
#define SYS_getpid					 9
#define SYS_proc_create				10
#define SYS_proc_run				11
#define SYS_proc_destroy			12
#define SYS_yield					13
#define SYS_change_vcore			14
#define SYS_fork					15
#define SYS_exec					16
#define SYS_waitpid					17
#define SYS_mmap					18
#define SYS_munmap					19
#define SYS_mprotect				20
/* // these are the other mmap related calls, some of which we'll implement
#define SYS_mincore // can read page tables instead
#define SYS_madvise
#define SYS_mlock
#define SYS_msync
*/
/* was SYS_brk						21 */
#define SYS_shared_page_alloc		22
#define SYS_shared_page_free		23
#define SYS_provision				24
#define SYS_notify					25
#define SYS_self_notify				26
#define SYS_halt_core				27
#define SYS_init_arsc				28
#define SYS_change_to_m				29
#define SYS_poke_ksched				30
#define SYS_abort_sysc				31
#define SYS_populate_va				32
#define SYS_abort_sysc_fd			33
#define SYS_setup_vmm				34
#define SYS_vc_entry				35
#define SYS_nanosleep				36
#define SYS_pop_ctx					37

/* FS Syscalls */
#define SYS_read				100
#define SYS_write				101
#define SYS_openat				102
#define SYS_close				103
#define SYS_fstat				104
#define SYS_stat				105
#define SYS_lstat				106
#define SYS_fcntl				107
#define SYS_access				108
#define SYS_umask				109
/* was SYS_chmod				110 */
#define SYS_llseek				111
#define SYS_link				112
#define SYS_unlink				113
#define SYS_symlink				114
#define SYS_readlink			115
#define SYS_chdir				116
#define SYS_getcwd				117
#define SYS_mkdir				118
#define SYS_rmdir				119
#define SYS_pipe				120

#define SYS_wstat				121
#define SYS_fwstat				122
#define SYS_rename				123
#define SYS_fchdir				124
#define SYS_dup_fds_to			125
#define SYS_tap_fds				126

/* Misc syscalls */
#define SYS_gettimeofday		140
#define SYS_tcgetattr			141
#define SYS_tcsetattr			142
#define SYS_setuid				143
#define SYS_setgid				144

/* hotness! */
#define SYS_nbind				145
#define SYS_nmount				146
#define SYS_nunmount			147
/* was SYS_something			148 */
#define SYS_fd2path				149

/* Syscalls we plan to remove someday */
#define SYS_cache_buster        200 

/* For Buster Measurement Flags */
#define BUSTER_SHARED			0x0001
#define BUSTER_STRIDED			0x0002
#define BUSTER_LOCKED			0x0004
#define BUSTER_PRINT_TICKS		0x0008
#define BUSTER_JUST_LOCKS		0x0010 // unimplemented

// for system calls that pass filenames
#define MAX_PATH_LEN 256

/* wstat flags, so the kernel knows what M fields to look at */
#define WSTAT_MODE				0x001
#define WSTAT_ATIME				0x002
#define WSTAT_MTIME				0x004
#define WSTAT_LENGTH			0x008
#define WSTAT_NAME				0x010
#define WSTAT_UID				0x020
#define WSTAT_GID				0x040
#define WSTAT_MUID				0x080
