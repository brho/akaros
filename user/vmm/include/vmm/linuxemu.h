#pragma once

typedef bool (*dune_syscall_t)(struct vm_trapframe *);

struct dune_sys_table_entry {
	dune_syscall_t call;
	const char *name;
};

struct linux_stat_amd64 {
	uint64_t  st_dev;
	uint64_t  st_ino;
	uint64_t  st_nlink;
	uint32_t  st_mode;
	uint32_t  st_uid;
	uint32_t  st_gid;
	int32_t   pad;
	uint64_t  st_rdev;
	int64_t   st_size;
	int64_t   st_blksize;
	int64_t   st_blocks;
	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	int64_t  unused[3];
};

#define DUNE_MAX_NUM_SYSCALLS 1024

extern struct dune_sys_table_entry dune_syscall_table[];

bool init_linuxemu(void);
void init_lemu_logging(int logging_level);
void destroy_lemu_logging(void);
void lemuprint(const uint32_t tid, uint64_t syscall_number,
               const bool isError, const char *fmt, ...);

bool dune_sys_write(struct vm_trapframe *tf);
bool dune_sys_read(struct vm_trapframe *tf);
