#ifndef UCONTEXT_H
#define UCONTEXT_H

#include <stdint.h>
#include <ros/arch/trapframe.h>
#include <threadlib_internal.h>
struct ucontext {
	struct user_trapframe utf;
    void *tls_desc;
	thread_t *thread;
}; 

struct ucontext* create_context(thread_t *t, void *entry_pt, void *stack_top);
void save_context(struct ucontext *uc);
void restore_context(struct ucontext *uc);
void destroy_context(struct ucontext *uc);
void print_context(struct ucontext *uc);

#endif
