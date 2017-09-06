#pragma once

typedef bool (*dune_syscall_t)(struct vm_trapframe *);

struct dune_sys_table_entry {
	dune_syscall_t call;
	const char *name;
};

#define DUNE_MAX_NUM_SYSCALLS 1024

// TODO: Remove this once we have better management for gpcs
#define MAX_GPCS 12

extern struct dune_sys_table_entry dune_syscall_table[];

bool init_syscall_table(void);
void init_lemu_logging(int logging_level);
void destroy_lemu_logging(void);
void lemuprint(const uint32_t tid, uint64_t syscall_number,
               const bool isError, const char *fmt, ...);

bool dune_sys_write(struct vm_trapframe *tf);
bool dune_sys_read(struct vm_trapframe *tf);
