#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <threadlib_internal.h>
#include <vcore.h>
#include <ucontext.h>


/* Maintain a static reference to the main threads tls region */
static void *__main_tls_desc;

struct ucontext* create_context(thread_t *t, void *entry_pt, void *stack_top)
{
	uint32_t vcoreid = vcore_id();

	/* Allocate a new context struct */
	struct ucontext *uc = malloc(sizeof(struct ucontext));
	if(!uc) return NULL;

	/* If we are the main thread, then current_thread has not been set yet, so
	 * we set the tls descriptor to the tls on the currently running core */
	if(current_thread == NULL) {
		/* Make sure this only executes once! */
		static int done = FALSE;
		assert(!done); done = TRUE;

		/* Set up the tls regions for the main thred */
		uc->tls_desc = get_tls_desc(vcoreid);
		__main_tls_desc = uc->tls_desc;

		/* And set the current thread in this tls region */
		current_thread = t;
	}
    /* Otherwise, allocate a new tls region for this context */
	else {
		/* Allocate the tls, freeing the whole uc if it fails */
    	uc->tls_desc = allocate_tls();
		if(!uc->tls_desc) {
			free(uc);
			return NULL;
		}
		/* Temporarily switch into the new the context's tls region to set the
		 * current_thread variable in that tls region */
		void *temp_tls = get_tls_desc(vcoreid);
		set_tls_desc(uc->tls_desc, vcoreid);
		current_thread = t;
		/* Then switch back to the origin one */
		set_tls_desc(temp_tls, vcoreid);
	}

	/* For good measure, also save the thread associated with this context in
	 * the ucontext struct */
	uc->thread = t;

	/* Initialize the trapframe for this context */
	init_user_tf(&uc->utf, (uint32_t)entry_pt, (uint32_t)stack_top);
	return uc;
}

void save_context(struct ucontext *uc)
{
	/* Save the trapframe for this context */
	save_ros_tf(&uc->utf);
}

void restore_context(struct ucontext *uc)
{
	uint32_t vcoreid = vcore_id();

	/* Save the thread we are about to restore into current_thread
	 * in this vcores tls region */
	current_thread = uc->thread;
	/* Set the proper tls descriptor for the context we are restoring */
    set_tls_desc(uc->tls_desc, vcoreid);
	/* Pop the trapframe */
	pop_ros_tf(&uc->utf, vcoreid);
}

void destroy_context(struct ucontext *uc)
{
    extern void _dl_deallocate_tls (void *tcb, bool dealloc_tcb) internal_function;

	/* Make sure we have passed in Non-Null pointers for our ucontext / tls */
    assert(uc);
    assert(uc->tls_desc);

	/* Dont' deallocate the main tls descriptor, glibc will do that */
	if(uc->tls_desc != __main_tls_desc)
    	_dl_deallocate_tls(uc->tls_desc, TRUE);
	free(uc);
}

void print_context(struct ucontext *uc)
{
	/* Just print the trapframe */
	print_trapframe(&uc->utf);
}

