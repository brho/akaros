#ifndef UCONTEXT_H
#define UCONTEXT_H

#include <stdint.h>
#include <ros/trapframe.h>
#include <threadlib_internal.h>
struct u_context {
	/* this seems rather screwed up, not inheriting a uthread */
	struct user_context u_ctx;
    void *tls_desc;
	thread_t *thread;
}; 

struct u_context* create_context(thread_t *t, void *entry_pt, void *stack_top);
void save_context(struct u_context *uc);
void restore_context(struct u_context *uc);
void destroy_context(struct u_context *uc);
void print_context(struct u_context *uc);

#endif
