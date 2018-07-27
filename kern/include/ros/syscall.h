#pragma once

#include <ros/bits/syscall.h>
#include <ros/arch/syscall.h>
#include <ros/event.h>
#include <ros/atomic.h>

/* Flags for an individual syscall. */
#define SC_DONE					0x0001		/* SC is done */
#define SC_PROGRESS				0x0002		/* SC made progress */
#define SC_UEVENT				0x0004		/* user has an ev_q */
#define SC_K_LOCK				0x0008		/* kernel locked sysc */
#define SC_ABORT				0x0010		/* syscall abort attempted */

#define MAX_ERRSTR_LEN			128
#define SYSTR_BUF_SZ			PGSIZE

struct syscall {
	unsigned int				num;
	int							err;			/* errno */
	long						retval;
	atomic_t					flags;
	struct event_queue			*ev_q;
	void						*u_data;
	long						arg0;
	long						arg1;
	long						arg2;
	long						arg3;
	long						arg4;
	long						arg5;
	char						errstr[MAX_ERRSTR_LEN];
};

static inline bool syscall_retval_is_error(unsigned int sysc_nr, long retval)
{
	switch (sysc_nr) {
	case SYS_getpcoreid:
	case SYS_getvcoreid:
	case SYS_reboot:
	case SYS_proc_yield:
	case SYS_vc_entry:
	case SYS_umask:
	case SYS_init_arsc:
		return false;
	case SYS_abort_sysc:
	case SYS_abort_sysc_fd:
		/* These two are a little weird */
		return false;
	case SYS_null:
	case SYS_block:
	case SYS_nanosleep:
	case SYS_cache_invalidate:
	case SYS_proc_run:
	case SYS_proc_destroy:
	case SYS_exec:
	case SYS_munmap:
	case SYS_mprotect:
	case SYS_notify:
	case SYS_self_notify:
	case SYS_send_event:
	case SYS_halt_core:
	case SYS_pop_ctx:
	case SYS_vmm_poke_guest:
	case SYS_poke_ksched:
	case SYS_llseek:
	case SYS_close:
	case SYS_fstat:
	case SYS_stat:
	case SYS_lstat:
	case SYS_access:
	case SYS_link:
	case SYS_unlink:
	case SYS_symlink:
	case SYS_chdir:
	case SYS_fchdir:
	case SYS_mkdir:
	case SYS_rmdir:
	case SYS_tcgetattr:
	case SYS_tcsetattr:
	case SYS_setuid:
	case SYS_setgid:
	case SYS_rename:
	case SYS_nunmount:
	case SYS_fd2path:
		return retval != 0;
	case SYS_proc_create:
	case SYS_change_vcore:
	case SYS_fork:
	case SYS_waitpid:
	case SYS_shared_page_alloc:
	case SYS_shared_page_free:
	case SYS_provision:
	case SYS_change_to_m:
	case SYS_vmm_ctl:
	case SYS_read:
	case SYS_write:
	case SYS_openat:
	case SYS_fcntl:
	case SYS_readlink:
	case SYS_getcwd:
	case SYS_nbind:
	case SYS_nmount:
	case SYS_wstat:
	case SYS_fwstat:
		return retval < 0;
	case SYS_mmap:
		return retval == -1; /* MAP_FAILED */
	case SYS_vmm_add_gpcs:
	case SYS_populate_va:
	case SYS_dup_fds_to:
	case SYS_tap_fds:
		return retval <= 0;
	};
	return true;
}

struct childfdmap {
	unsigned int				parentfd;
	unsigned int				childfd;
	int							ok;
};

struct argenv {
	size_t argc;
	size_t envc;
	char buf[];
	/* The buf array is laid out as follows:
	 * buf {
	 *   char *argv[argc]; // Offset of arg relative to &argbuf[0]
	 *   char *envp[envc]; // Offset of envvar relative to &argbuf[0]
	 *   char argbuf[sum(map(strlen + 1, argv + envp))];
	 * }
	 */
};

#ifndef ROS_KERNEL

/* Temp hack, til the rest of glibc/userspace uses sys/syscall.h */
#include <sys/syscall.h>
#endif /* ifndef ROS_KERNEL */
