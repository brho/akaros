#pragma once

typedef bool (*dune_syscall_t)(struct vm_trapframe *);

struct dune_sys_table_entry {
	dune_syscall_t call;
	const char *name;
};

#define dune_max_syscall 1024

struct dune_sys_table_entry dune_syscall_table[dune_max_syscall];

void init_syscall_table(void);
bool dune_sys_write(struct vm_trapframe *tf);
bool dune_sys_read(struct vm_trapframe *tf);
