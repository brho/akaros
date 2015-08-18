// Main public header file for our user-land support library,
// whose code lives in the lib directory.
// This library is roughly our OS's version of a standard C library,
// and is intended to be linked into all user-mode applications
// (NOT the kernel or boot loader).

#ifndef PARLIB_PARLIB_H
#define PARLIB_PARLIB_H

#ifndef __ASSEMBLER__

#include <parlib/common.h>
#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/procinfo.h>
#include <ros/procdata.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>
#include <parlib/ros_debug.h>
#include <ros/fdtap.h>

__BEGIN_DECLS

enum {
	PG_RDONLY = 4,
	PG_RDWR   = 6,
};

ssize_t     sys_cputs(const uint8_t *s, size_t len);
uint16_t    sys_cgetc(void);
int         sys_null(void);
size_t      sys_getpcoreid(void);
/* Process Management */
int         sys_getpid(void);
int         sys_proc_destroy(int pid, int exitcode);
void        sys_yield(bool being_nice);
int         sys_proc_create(char *path, size_t path_l, char *argv[],
                            char *envp[], int flags);
int         sys_proc_run(int pid);
ssize_t     sys_shared_page_alloc(void **addr, pid_t p2, 
                                  int p1_flags, int p2_flags);
ssize_t     sys_shared_page_free(void *addr, pid_t p2);
void        sys_reboot();
void 		*sys_mmap(void *addr, size_t length, int prot, int flags,
                      int fd, size_t offset);
int			sys_provision(int pid, unsigned int res_type, long res_val);
int         sys_notify(int pid, unsigned int ev_type, struct event_msg *u_msg);
int         sys_self_notify(uint32_t vcoreid, unsigned int ev_type,
                            struct event_msg *u_msg, bool priv);
int         sys_halt_core(unsigned int usec);
void*		sys_init_arsc();
int         sys_block(unsigned int usec);
int         sys_change_vcore(uint32_t vcoreid, bool enable_my_notif);
int         sys_change_to_m(void);
int         sys_poke_ksched(int pid, unsigned int res_type);
int         sys_abort_sysc(struct syscall *sysc);
int         sys_abort_sysc_fd(int fd);
int         sys_tap_fds(struct fd_tap_req *tap_reqs, size_t nr_reqs);

long		syscall_async(struct syscall *sysc, unsigned long num, ...);

/* Posix signal related stuff */
struct sigdata {
	struct user_context u_ctx;
	struct ancillary_state as;
	struct siginfo info;
	void *stack;
};
/* This is the set of global function pointers that a 2LS using signals needs
 * to implement.  Subject to grow over time. */
struct signal_ops {
	int (*sigprocmask)(int, __const sigset_t *__restrict, sigset_t *__restrict);
};
extern struct signal_ops *signal_ops;
void init_posix_signals(void);
struct sigdata *alloc_sigdata();
void free_sigdata(struct sigdata *sigdata);
void trigger_posix_signal(int sig_nr, struct siginfo *info, void *aux);

/* Control variables */
extern bool parlib_wants_to_be_mcp;	/* instructs the 2LS to be an MCP */

__END_DECLS

#endif	// !ASSEMBLER

#endif /* PARLIB_PARLIB_H */
