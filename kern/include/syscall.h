#ifndef ROS_KERN_SYSCALL_H
#define ROS_KERN_SYSCALL_H
#ifndef ROS_KERNEL
# error "This is ROS kernel header; user programs should not #include it"
#endif

#include <ros/common.h>
#include <process.h>

#define SYSTRACE_ON					0x01
#define SYSTRACE_LOUD				0x02
#define SYSTRACE_ALLPROC			0x04

#define MAX_NUM_TRACED				10
#define MAX_SYSTRACES				1024

#define MAX_ASRC_BATCH				10

/* Consider cache aligning this */
struct systrace_record {
	uint64_t		timestamp;
	uintreg_t		syscallno;
	uintreg_t		arg0;
	uintreg_t		arg1;
	uintreg_t		arg2;
	uintreg_t		arg3;
	uintreg_t		arg4;
	uintreg_t		arg5;
	int				pid;
	uint32_t		coreid;
	uint32_t		vcoreid;
};

/* Syscall table */
typedef intreg_t (*syscall_t)(struct proc *, uintreg_t, uintreg_t, uintreg_t,
                              uintreg_t, uintreg_t, uintreg_t);
struct sys_table_entry {
	syscall_t call;
	char *name;
};
const static struct sys_table_entry syscall_table[];
/* Syscall invocation */
void prep_syscalls(struct proc *p, struct syscall *sysc, unsigned int nr_calls);
void run_local_syscall(struct syscall *sysc);
intreg_t syscall(struct proc *p, uintreg_t sc_num, uintreg_t a0, uintreg_t a1,
                 uintreg_t a2, uintreg_t a3, uintreg_t a4, uintreg_t a5);
void set_errno(int errno);
void signal_syscall(struct syscall *sysc, struct proc *p);

/* Tracing functions */
void systrace_start(bool silent);
void systrace_stop(void);
int systrace_reg(bool all, struct proc *p);
int systrace_dereg(bool all, struct proc *p);
void systrace_print(bool all, struct proc *p);
void systrace_clear_buffer(void);

#endif /* !ROS_KERN_SYSCALL_H */
