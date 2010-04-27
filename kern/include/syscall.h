#ifndef ROS_KERN_SYSCALL_H
#define ROS_KERN_SYSCALL_H
#ifndef ROS_KERNEL
# error "This is ROS kernel header; user programs should not #include it"
#endif

#include <ros/syscall.h>
#include <process.h>

#define ERR_PTR(err)  ((void *)((uintptr_t)(err)))
#define PTR_ERR(ptr)  ((uintptr_t)(ptr))
#define IS_ERR(ptr)   ((uintptr_t)-(uintptr_t)(ptr) < 512)

#define SYSTRACE_ON					0x01
#define SYSTRACE_LOUD				0x02
#define SYSTRACE_ALLPROC			0x04

#define MAX_NUM_TRACED				10
#define MAX_SYSTRACES				1024

/* Consider cache aligning this */
struct systrace_record {
	uint64_t		timestamp;
	uintreg_t		syscallno;
	int				pid;
	uint32_t		coreid;
	uint32_t		vcoreid;
};

intreg_t syscall(struct proc *p, uintreg_t num, uintreg_t a1, uintreg_t a2,
                 uintreg_t a3, uintreg_t a4, uintreg_t a5);
intreg_t syscall_async(env_t* e, syscall_req_t *syscall);
intreg_t process_generic_syscalls(env_t* e, size_t max);

/* Tracing functions */
void systrace_start(bool silent);
void systrace_stop(void);
int systrace_reg(bool all, struct proc *p);
int systrace_dereg(bool all, struct proc *p);
void systrace_print(bool all, struct proc *p);
void systrace_clear_buffer(void);

intreg_t sys_read(struct proc* p, int fd, void* buf, int len);
intreg_t sys_write(struct proc* p, int fd, const void* buf, int len);
intreg_t sys_pread(struct proc* p, int fd, void* buf, int len, int offset);
intreg_t sys_pwrite(struct proc* p, int fd, const void* buf, int len, int offset);
intreg_t sys_open(struct proc* p, const char* path, int oflag, int mode);
intreg_t sys_close(struct proc* p, int fd);
intreg_t sys_fstat(struct proc* p, int fd, void* buf);
intreg_t sys_stat(struct proc* p, const char* path, void* buf);
intreg_t sys_lstat(struct proc* p, const char* path, void* buf);
intreg_t sys_fcntl(struct proc* p, int fd, int cmd, int arg);
intreg_t sys_access(struct proc* p, const char* path, int type);
intreg_t sys_umask(struct proc* p, int mask);
intreg_t sys_chmod(struct proc* p, const char* path, int mode);
intreg_t sys_lseek(struct proc* p, int fd, int offset, int whence);
intreg_t sys_link(struct proc* p, const char* old, const char* new);
intreg_t sys_unlink(struct proc* p, const char* path);
intreg_t sys_chdir(struct proc* p, const char* path);
intreg_t sys_getcwd(struct proc* p, char* pwd, int size);
intreg_t sys_gettimeofday(struct proc* p, int* buf);
intreg_t sys_tcsetattr(struct proc* p, int fd, int optional_actions, const void* termios_p);
intreg_t sys_tcgetattr(struct proc* p, int fd, void* termios_p);
intreg_t sys_exec(struct proc* p, int fd, procinfo_t* pi);
#endif /* !ROS_KERN_SYSCALL_H */
