#include <stdlib.h>
#include <sys/tls.h>
#include <parlib/vcore.h>
#include <ldsodefs.h>

void set_tls_desc(void* addr)
{
	__set_tls_desc(addr);
}

void *get_tls_desc(void)
{
	return __get_tls_desc();
}

/* Get a TLS, returns 0 on failure.  Vcores have their own TLS, and any thread
 * created by a user-level scheduler needs to create a TLS as well. */
void *allocate_tls(void)
{
	void *tcb = _dl_allocate_tls(NULL);
	if (!tcb)
		return 0;
#ifdef TLS_TCB_AT_TP
	/* Make sure the TLS is set up properly - its tcb pointer points to
	 * itself.  Keep this in sync with sysdeps/akaros/XXX/tls.h.  For
	 * whatever reason, dynamically linked programs do not need this to be
	 * redone, but statics do. */
	tcbhead_t *head = (tcbhead_t*)tcb;
	head->tcb = tcb;
	head->self = tcb;
	head->pointer_guard = THREAD_SELF->header.pointer_guard;
	head->stack_guard = THREAD_SELF->header.stack_guard;
#endif
	return tcb;
}

/* Free a previously allocated TLS region */
void free_tls(void *tcb)
{
	_dl_deallocate_tls(tcb, TRUE);
}

/* Reinitialize / reset / refresh a TLS to its initial values.  This doesn't do
 * it properly yet, it merely frees and re-allocates the TLS, which is why we're
 * slightly ghetto and return the pointer you should use for the TCB. */
void *reinit_tls(void *tcb)
{
	/* TODO: keep this in sync with the methods used in
	 * allocate_transition_tls() */
	free_tls(tcb);
	return allocate_tls();
}
