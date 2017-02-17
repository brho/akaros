/* See COPYRIGHT for copyright information. */

//#define DEBUG
#include <ros/common.h>
#include <ros/limits.h>
#include <arch/types.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <arch/console.h>
#include <time.h>
#include <error.h>

#include <elf.h>
#include <string.h>
#include <assert.h>
#include <process.h>
#include <schedule.h>
#include <pmap.h>
#include <umem.h>
#include <mm.h>
#include <trap.h>
#include <syscall.h>
#include <kmalloc.h>
#include <profiler.h>
#include <stdio.h>
#include <frontend.h>
#include <hashtable.h>
#include <bitmask.h>
#include <vfs.h>
#include <devfs.h>
#include <smp.h>
#include <arsc_server.h>
#include <event.h>
#include <kprof.h>
#include <termios.h>
#include <manager.h>
#include <ros/procinfo.h>

static int execargs_stringer(struct proc *p, char *d, size_t slen,
			     char *path, size_t path_l,
			     char *argenv, size_t argenv_l);

/* Global, used by the kernel monitor for syscall debugging. */
bool systrace_loud = FALSE;

/* Helper, given the trace record, pretty-print the trace's contents into the
 * trace's pretty buf.  'entry' says whether we're an entry record or not
 * (exit).  Returns the number of bytes put into the pretty_buf. */
static size_t systrace_fill_pretty_buf(struct systrace_record *trace,
                                       bool entry)
{
	size_t len = 0;
	struct timespec ts_start = tsc2timespec(trace->start_timestamp);
	struct timespec ts_end = tsc2timespec(trace->end_timestamp);

	/* Slightly different formats between entry and exit.  Entry has retval set
	 * to ---, and begins with E.  Exit begins with X. */
	if (entry) {
		len = snprintf(trace->pretty_buf, SYSTR_PRETTY_BUF_SZ - len,
		      "E [%7d.%09d]-[%7d.%09d] Syscall %3d (%12s):(0x%llx, 0x%llx, "
		      "0x%llx, 0x%llx, 0x%llx, 0x%llx) ret: --- proc: %d core: %d "
		      "vcore: %d data: ",
		               ts_start.tv_sec,
		               ts_start.tv_nsec,
		               ts_end.tv_sec,
		               ts_end.tv_nsec,
		               trace->syscallno,
		               syscall_table[trace->syscallno].name,
		               trace->arg0,
		               trace->arg1,
		               trace->arg2,
		               trace->arg3,
		               trace->arg4,
		               trace->arg5,
		               trace->pid,
		               trace->coreid,
		               trace->vcoreid);
	} else {
		len = snprintf(trace->pretty_buf, SYSTR_PRETTY_BUF_SZ - len,
		      "X [%7d.%09d]-[%7d.%09d] Syscall %3d (%12s):(0x%llx, 0x%llx, "
		      "0x%llx, 0x%llx, 0x%llx, 0x%llx) ret: 0x%llx proc: %d core: %d "
		      "vcore: %d data: ",
		               ts_start.tv_sec,
		               ts_start.tv_nsec,
		               ts_end.tv_sec,
		               ts_end.tv_nsec,
		               trace->syscallno,
		               syscall_table[trace->syscallno].name,
		               trace->arg0,
		               trace->arg1,
		               trace->arg2,
		               trace->arg3,
		               trace->arg4,
		               trace->arg5,
		               trace->retval,
		               trace->pid,
		               trace->coreid,
		               trace->vcoreid);
	}
	len += printdump(trace->pretty_buf + len, trace->datalen,
	                 SYSTR_PRETTY_BUF_SZ - len - 1,
	                 trace->data);
	len += snprintf(trace->pretty_buf + len, SYSTR_PRETTY_BUF_SZ - len, "\n");
	return len;
}

/* If some syscalls block, then they can really hurt the user and the
 * kernel.  For instance, if you blocked another call because the trace queue is
 * full, the 2LS will want to yield the vcore, but then *that* call would block
 * too.  Since that caller was in vcore context, the core will just spin
 * forever.
 *
 * Even worse, some syscalls operate on the calling core or current context,
 * thus accessing pcpui.  If we block, then that old context is gone.  Worse, we
 * could migrate and then be operating on a different core.  Imagine
 * SYS_halt_core.  Doh! */
static bool sysc_can_block(unsigned int sysc_num)
{
	switch (sysc_num) {
	case SYS_proc_yield:
	case SYS_fork:
	case SYS_exec:
	case SYS_pop_ctx:
	case SYS_getvcoreid:
	case SYS_halt_core:
	case SYS_vc_entry:
	case SYS_change_vcore:
	case SYS_change_to_m:
		return FALSE;
	}
	return TRUE;
}

/* Helper: spits out our trace to the various sinks. */
static void systrace_output(struct systrace_record *trace,
                            struct strace *strace, bool entry)
{
	ERRSTACK(1);
	size_t pretty_len;

	/* qio ops can throw, especially the blocking qwrite.  I had it block on the
	 * outbound path of sys_proc_destroy().  The rendez immediately throws. */
	if (waserror()) {
		poperror();
		return;
	}
	pretty_len = systrace_fill_pretty_buf(trace, entry);
	if (strace) {
		/* At this point, we're going to emit the exit trace.  It's just a
		 * question of whether or not we block while doing it. */
		if (strace->drop_overflow || !sysc_can_block(trace->syscallno))
			qiwrite(strace->q, trace->pretty_buf, pretty_len);
		else
			qwrite(strace->q, trace->pretty_buf, pretty_len);
	}
	if (systrace_loud)
		printk("%s", trace->pretty_buf);
	poperror();
}

static bool should_strace(struct proc *p, struct syscall *sysc)
{
	unsigned int sysc_num;

	if (systrace_loud)
		return TRUE;
	if (!p->strace || !p->strace->tracing)
		return FALSE;
	/* TOCTTOU concerns - sysc is __user. */
	sysc_num = ACCESS_ONCE(sysc->num);
	if (qfull(p->strace->q)) {
		if (p->strace->drop_overflow || !sysc_can_block(sysc_num)) {
			atomic_inc(&p->strace->nr_drops);
			return FALSE;
		}
	}
	if (sysc_num > MAX_SYSCALL_NR)
		return FALSE;
	return test_bit(sysc_num, p->strace->trace_set);
}

/* Starts a trace for p running sysc, attaching it to kthread.  Pairs with
 * systrace_finish_trace(). */
static void systrace_start_trace(struct kthread *kthread, struct syscall *sysc)
{
	struct proc *p = current;
	struct systrace_record *trace;
	uintreg_t data_arg;
	size_t data_len = 0;

	kthread->strace = 0;
	if (!should_strace(p, sysc))
		return;
	/* TODO: consider a block_alloc and qpass, though note that we actually
	 * write the same trace in twice (entry and exit). */
	trace = kpages_alloc(SYSTR_BUF_SZ, MEM_ATOMIC);
	if (p->strace) {
		if (!trace) {
			atomic_inc(&p->strace->nr_drops);
			return;
		}
		/* Avoiding the atomic op.  We sacrifice accuracy for less overhead. */
		p->strace->appx_nr_sysc++;
	} else {
		if (!trace)
			return;
	}
	/* if you ever need to debug just one strace function, this is
	 * handy way to do it: just bail out if it's not the one you
	 * want.
	 * if (sysc->num != SYS_exec)
	 * return; */
	trace->start_timestamp = read_tsc();
	trace->end_timestamp = 0;
	trace->syscallno = sysc->num;
	trace->arg0 = sysc->arg0;
	trace->arg1 = sysc->arg1;
	trace->arg2 = sysc->arg2;
	trace->arg3 = sysc->arg3;
	trace->arg4 = sysc->arg4;
	trace->arg5 = sysc->arg5;
	trace->retval = 0;
	trace->pid = p->pid;
	trace->coreid = core_id();
	trace->vcoreid = proc_get_vcoreid(p);
	trace->pretty_buf = (char*)trace + sizeof(struct systrace_record);
	trace->datalen = 0;
	trace->data[0] = 0;

	switch (sysc->num) {
	case SYS_write:
		data_arg = sysc->arg1;
		data_len = sysc->arg2;
		break;
	case SYS_openat:
		data_arg = sysc->arg1;
		data_len = sysc->arg2;
		break;
	case SYS_exec:
		trace->datalen = execargs_stringer(current,
						   (char *)trace->data,
						   sizeof(trace->data),
						   (char *)sysc->arg0,
						   sysc->arg1,
						   (char *)sysc->arg2,
						   sysc->arg3);
		break;
	case SYS_proc_create:
		trace->datalen = execargs_stringer(current,
						   (char *)trace->data,
						   sizeof(trace->data),
						   (char *)sysc->arg0,
						   sysc->arg1,
						   (char *)sysc->arg2,
						   sysc->arg3);
		break;
	}
	if (data_len) {
		trace->datalen = MIN(sizeof(trace->data), data_len);
		copy_from_user(trace->data, (void*)data_arg, trace->datalen);
	}

	systrace_output(trace, p->strace, TRUE);

	kthread->strace = trace;
}

/* Finishes the trace on kthread for p, with retval being the return from the
 * syscall we're tracing.  Pairs with systrace_start_trace(). */
static void systrace_finish_trace(struct kthread *kthread, long retval)
{
	struct proc *p = current;
	struct systrace_record *trace;
	long data_arg;
	size_t data_len = 0;

	if (!kthread->strace)
		return;
	trace = kthread->strace;
	trace->end_timestamp = read_tsc();
	trace->retval = retval;

	/* Only try to do the trace data if we didn't do it on entry */
	if (!trace->datalen) {
		switch (trace->syscallno) {
		case SYS_read:
			data_arg = trace->arg1;
			data_len = retval < 0 ? 0 : retval;
			break;
		}
		trace->datalen = MIN(sizeof(trace->data), data_len);
		if (trace->datalen)
			copy_from_user(trace->data, (void*)data_arg, trace->datalen);
	}

	systrace_output(trace, p->strace, FALSE);
	kpages_free(kthread->strace, SYSTR_BUF_SZ);
	kthread->strace = 0;
}

#ifdef CONFIG_SYSCALL_STRING_SAVING

static void alloc_sysc_str(struct kthread *kth)
{
	kth->name = kmalloc(SYSCALL_STRLEN, MEM_ATOMIC);
	if (!kth->name)
		return;
	kth->name[0] = 0;
}

static void free_sysc_str(struct kthread *kth)
{
	char *str = kth->name;

	kth->name = 0;
	kfree(str);
}

#define sysc_save_str(...)                                                     \
{                                                                              \
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];                     \
                                                                               \
	if (pcpui->cur_kthread->name)                                              \
		snprintf(pcpui->cur_kthread->name, SYSCALL_STRLEN, __VA_ARGS__);       \
}

#else

static void alloc_sysc_str(struct kthread *kth)
{
}

static void free_sysc_str(struct kthread *kth)
{
}

#define sysc_save_str(...)

#endif /* CONFIG_SYSCALL_STRING_SAVING */

/* Helper to finish a syscall, signalling if appropriate */
static void finish_sysc(struct syscall *sysc, struct proc *p)
{
	/* Atomically turn on the LOCK and SC_DONE flag.  The lock tells userspace
	 * we're messing with the flags and to not proceed.  We use it instead of
	 * CASing with userspace.  We need the atomics since we're racing with
	 * userspace for the event_queue registration.  The 'lock' tells userspace
	 * to not muck with the flags while we're signalling. */
	atomic_or(&sysc->flags, SC_K_LOCK | SC_DONE);
	__signal_syscall(sysc, p);
	atomic_and(&sysc->flags, ~SC_K_LOCK);
}

/* Helper that "finishes" the current async syscall.  This should be used with
 * care when we are not using the normal syscall completion path.
 *
 * Do *NOT* complete the same syscall twice.  This is catastrophic for _Ms, and
 * a bad idea for _S.
 *
 * It is possible for another user thread to see the syscall being done early -
 * they just need to be careful with the weird proc management calls (as in,
 * don't trust an async fork).
 *
 * *sysc is in user memory, and should be pinned (TODO: UMEM).  There may be
 * issues with unpinning this if we never return. */
static void finish_current_sysc(int retval)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	assert(pcpui->cur_kthread->sysc);
	pcpui->cur_kthread->sysc->retval = retval;
	finish_sysc(pcpui->cur_kthread->sysc, pcpui->cur_proc);
}

/* Callable by any function while executing a syscall (or otherwise, actually).
 */
void set_errno(int errno)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (pcpui->cur_kthread && pcpui->cur_kthread->sysc)
		pcpui->cur_kthread->sysc->err = errno;
}

/* Callable by any function while executing a syscall (or otherwise, actually).
 */
int get_errno(void)
{
	/* if there's no errno to get, that's not an error I guess. */
	int errno = 0;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (pcpui->cur_kthread && pcpui->cur_kthread->sysc)
		errno = pcpui->cur_kthread->sysc->err;
	return errno;
}

void unset_errno(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (!pcpui->cur_kthread || !pcpui->cur_kthread->sysc)
		return;
	pcpui->cur_kthread->sysc->err = 0;
	pcpui->cur_kthread->sysc->errstr[0] = '\0';
}

void vset_errstr(const char *fmt, va_list ap)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	if (!pcpui->cur_kthread || !pcpui->cur_kthread->sysc)
		return;

	vsnprintf(pcpui->cur_kthread->sysc->errstr, MAX_ERRSTR_LEN, fmt, ap);

	/* TODO: likely not needed */
	pcpui->cur_kthread->sysc->errstr[MAX_ERRSTR_LEN - 1] = '\0';
}

void set_errstr(const char *fmt, ...)
{
	va_list ap;

	assert(fmt);
	va_start(ap, fmt);
	vset_errstr(fmt, ap);
	va_end(ap);
}

char *current_errstr(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (!pcpui->cur_kthread || !pcpui->cur_kthread->sysc)
		return "no errstr";
	return pcpui->cur_kthread->sysc->errstr;
}

void set_error(int error, const char *fmt, ...)
{
	va_list ap;

	set_errno(error);

	assert(fmt);
	va_start(ap, fmt);
	vset_errstr(fmt, ap);
	va_end(ap);
}

struct errbuf *get_cur_errbuf(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	return pcpui->cur_kthread->errbuf;
}

void set_cur_errbuf(struct errbuf *ebuf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	pcpui->cur_kthread->errbuf = ebuf;
}

char *get_cur_genbuf(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	assert(pcpui->cur_kthread);
	return pcpui->cur_kthread->generic_buf;
}

/* Helper, looks up proc* for pid and ensures p controls that proc. 0 o/w */
static struct proc *get_controllable_proc(struct proc *p, pid_t pid)
{
	struct proc *target = pid2proc(pid);
	if (!target) {
		set_errno(ESRCH);
		return 0;
	}
	if (!proc_controls(p, target)) {
		set_errno(EPERM);
		proc_decref(target);
		return 0;
	}
	return target;
}

static int unpack_argenv(struct argenv *argenv, size_t argenv_l,
                         int *argc_p, char ***argv_p,
                         int *envc_p, char ***envp_p)
{
	int argc = argenv->argc;
	int envc = argenv->envc;
	char **argv = (char**)argenv->buf;
	char **envp = argv + argc;
	char *argbuf = (char*)(envp + envc);
	uintptr_t argbuf_offset = (uintptr_t)(argbuf - (char*)(argenv));

	if (((char*)argv - (char*)argenv) > argenv_l)
		return -1;
	if (((char*)argv + (argc * sizeof(char**)) - (char*)argenv) > argenv_l)
		return -1;
	if (((char*)envp - (char*)argenv) > argenv_l)
		return -1;
	if (((char*)envp + (envc * sizeof(char**)) - (char*)argenv) > argenv_l)
		return -1;
	if (((char*)argbuf - (char*)argenv) > argenv_l)
		return -1;
	for (int i = 0; i < argc; i++) {
		if ((uintptr_t)(argv[i] + argbuf_offset) > argenv_l)
			return -1;
		argv[i] += (uintptr_t)argbuf;
	}
	for (int i = 0; i < envc; i++) {
		if ((uintptr_t)(envp[i] + argbuf_offset) > argenv_l)
			return -1;
		envp[i] += (uintptr_t)argbuf;
	}
	*argc_p = argc;
	*argv_p = argv;
	*envc_p = envc;
	*envp_p = envp;
	return 0;
}

/************** Utility Syscalls **************/

static int sys_null(void)
{
	return 0;
}

/* Diagnostic function: blocks the kthread/syscall, to help userspace test its
 * async I/O handling. */
static int sys_block(struct proc *p, unsigned long usec)
{
	sysc_save_str("block for %lu usec", usec);
	kthread_usleep(usec);
	return 0;
}

/* Pause execution for a number of nanoseconds.
 * The current implementation rounds up to the nearest microsecond. If the
 * syscall is aborted, we return the remaining time the call would have ran
 * in the 'rem' parameter.  */
static int sys_nanosleep(struct proc *p,
                         const struct timespec *req,
                         struct timespec *rem)
{
	ERRSTACK(1);
	uint64_t usec;
	struct timespec kreq, krem = {0, 0};
	uint64_t tsc = read_tsc();

	/* Check the input arguments. */
	if (memcpy_from_user(p, &kreq, req, sizeof(struct timespec))) {
		set_errno(EFAULT);
		return -1;
	}
	if (rem && memcpy_to_user(p, rem, &krem, sizeof(struct timespec))) {
		set_errno(EFAULT);
		return -1;
	}
	if (kreq.tv_sec < 0) {
		set_errno(EINVAL);
		return -1;
	}
	if ((kreq.tv_nsec < 0) || (kreq.tv_nsec > 999999999)) {
		set_errno(EINVAL);
		return -1;
	}

	/* Convert timespec to usec. Ignore overflow on the tv_sec field. */
	usec = kreq.tv_sec * 1000000;
	usec += DIV_ROUND_UP(kreq.tv_nsec, 1000);

	/* Attempt to sleep. If we get aborted, copy the remaining time into
	 * 'rem' and return. We assume the tsc is sufficient to tell how much
	 * time is remaining (i.e. it only overflows on the order of hundreds of
	 * years, which should be sufficiently long enough to ensure we don't
	 * overflow). */
	if (waserror()) {
		krem = tsc2timespec(read_tsc() - tsc);
		if (rem && memcpy_to_user(p, rem, &krem, sizeof(struct timespec)))
			set_errno(EFAULT);
		poperror();
		return -1;
	}
	sysc_save_str("nanosleep for %d usec", usec);
	kthread_usleep(usec);
	poperror();
	return 0;
}

static int sys_cache_invalidate(void)
{
	#ifdef CONFIG_X86
		wbinvd();
	#endif
	return 0;
}

/* sys_reboot(): called directly from dispatch table. */

/* Returns the id of the physical core this syscall is executed on. */
static uint32_t sys_getpcoreid(void)
{
	return core_id();
}

// TODO: Temporary hack until thread-local storage is implemented on i386 and
// this is removed from the user interface
static size_t sys_getvcoreid(struct proc *p)
{
	return proc_get_vcoreid(p);
}

/************** Process management syscalls **************/

/* Helper for proc_create and fork */
static void inherit_strace(struct proc *parent, struct proc *child)
{
	if (parent->strace && parent->strace->inherit) {
		/* Refcnt on both, put in the child's ->strace. */
		kref_get(&parent->strace->users, 1);
		kref_get(&parent->strace->procs, 1);
		child->strace = parent->strace;
	}
}

/* Creates a process from the file 'path'.  The process is not runnable by
 * default, so it needs it's status to be changed so that the next call to
 * schedule() will try to run it. */
static int sys_proc_create(struct proc *p, char *path, size_t path_l,
                           char *argenv, size_t argenv_l, int flags)
{
	int pid = 0;
	char *t_path;
	struct file *program;
	struct proc *new_p;
	int argc, envc;
	char **argv, **envp;
	struct argenv *kargenv;

	t_path = copy_in_path(p, path, path_l);
	if (!t_path)
		return -1;
	/* TODO: 9ns support */
	program = do_file_open(t_path, O_READ, 0);
	if (!program)
		goto error_with_path;
	if (!is_valid_elf(program)) {
		set_errno(ENOEXEC);
		goto error_with_file;
	}
	/* Check the size of the argenv array, error out if too large. */
	if ((argenv_l < sizeof(struct argenv)) || (argenv_l > ARG_MAX)) {
		set_error(EINVAL, "The argenv array has an invalid size: %lu\n",
				  argenv_l);
		goto error_with_file;
	}
	/* Copy the argenv array into a kernel buffer. Delay processing of the
	 * array to load_elf(). */
	kargenv = user_memdup_errno(p, argenv, argenv_l);
	if (!kargenv) {
		set_error(EINVAL, "Failed to copy in the args");
		goto error_with_file;
	}
	/* Unpack the argenv array into more usable variables. Integrity checking
	 * done along side this as well. */
	if (unpack_argenv(kargenv, argenv_l, &argc, &argv, &envc, &envp)) {
		set_error(EINVAL, "Failed to unpack the args");
		goto error_with_kargenv;
	}
	/* TODO: need to split the proc creation, since you must load after setting
	 * args/env, since auxp gets set up there. */
	//new_p = proc_create(program, 0, 0);
	if (proc_alloc(&new_p, current, flags)) {
		set_error(ENOMEM, "Failed to alloc new proc");
		goto error_with_kargenv;
	}
	inherit_strace(p, new_p);
	/* close the CLOEXEC ones, even though this isn't really an exec */
	close_fdt(&new_p->open_files, TRUE);
	/* Load the elf. */
	if (load_elf(new_p, program, argc, argv, envc, envp)) {
		set_error(EINVAL, "Failed to load elf");
		goto error_with_proc;
	}
	/* progname is argv0, which accounts for symlinks */
	proc_set_progname(new_p, argc ? argv[0] : NULL);
	proc_replace_binary_path(new_p, t_path);
	kref_put(&program->f_kref);
	user_memdup_free(p, kargenv);
	__proc_ready(new_p);
	pid = new_p->pid;
	profiler_notify_new_process(new_p);
	proc_decref(new_p);	/* give up the reference created in proc_create() */
	return pid;
error_with_proc:
	/* proc_destroy will decref once, which is for the ref created in
	 * proc_create().  We don't decref again (the usual "+1 for existing"),
	 * since the scheduler, which usually handles that, hasn't heard about the
	 * process (via __proc_ready()). */
	proc_destroy(new_p);
error_with_kargenv:
	user_memdup_free(p, kargenv);
error_with_file:
	kref_put(&program->f_kref);
error_with_path:
	free_path(p, t_path);
	return -1;
}

/* Makes process PID runnable.  Consider moving the functionality to process.c */
static error_t sys_proc_run(struct proc *p, unsigned pid)
{
	error_t retval = 0;
	struct proc *target = get_controllable_proc(p, pid);
	if (!target)
		return -1;
	if (target->state != PROC_CREATED) {
		set_errno(EINVAL);
		proc_decref(target);
		return -1;
	}
	/* Note a proc can spam this for someone it controls.  Seems safe - if it
	 * isn't we can change it. */
	proc_wakeup(target);
	proc_decref(target);
	return 0;
}

/* Destroy proc pid.  If this is called by the dying process, it will never
 * return.  o/w it will return 0 on success, or an error.  Errors include:
 * - ESRCH: if there is no such process with pid
 * - EPERM: if caller does not control pid */
static error_t sys_proc_destroy(struct proc *p, pid_t pid, int exitcode)
{
	error_t r;
	struct proc *p_to_die = get_controllable_proc(p, pid);
	if (!p_to_die)
		return -1;
	if (p_to_die == p) {
		p->exitcode = exitcode;
		printd("[PID %d] proc exiting gracefully (code %d)\n", p->pid,exitcode);
	} else {
		p_to_die->exitcode = exitcode; 	/* so its parent has some clue */
		printd("[%d] destroying proc %d\n", p->pid, p_to_die->pid);
	}
	proc_destroy(p_to_die);
	proc_decref(p_to_die);
	return 0;
}

static int sys_proc_yield(struct proc *p, bool being_nice)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* proc_yield() often doesn't return - we need to set the syscall retval
	 * early.  If it doesn't return, it expects to eat our reference (for now).
	 */
	free_sysc_str(pcpui->cur_kthread);
	systrace_finish_trace(pcpui->cur_kthread, 0);
	finish_sysc(pcpui->cur_kthread->sysc, pcpui->cur_proc);
	pcpui->cur_kthread->sysc = 0;	/* don't touch sysc again */
	proc_incref(p, 1);
	proc_yield(p, being_nice);
	proc_decref(p);
	/* Shouldn't return, to prevent the chance of mucking with cur_sysc. */
	smp_idle();
	assert(0);
}

static int sys_change_vcore(struct proc *p, uint32_t vcoreid,
                             bool enable_my_notif)
{
	/* Note retvals can be negative, but we don't mess with errno in case
	 * callers use this in low-level code and want to extract the 'errno'. */
	return proc_change_to_vcore(p, vcoreid, enable_my_notif);
}

static ssize_t sys_fork(env_t* e)
{
	uintptr_t temp;
	int ret;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	// TODO: right now we only support fork for single-core processes
	if (e->state != PROC_RUNNING_S) {
		set_errno(EINVAL);
		return -1;
	}
	env_t* env;
	ret = proc_alloc(&env, current, PROC_DUP_FGRP);
	assert(!ret);
	assert(env != NULL);
	proc_set_progname(env, e->progname);

	/* Can't really fork if we don't have a current_ctx to fork */
	if (!current_ctx) {
		proc_destroy(env);
		proc_decref(env);
		set_errno(EINVAL);
		return -1;
	}
	assert(pcpui->cur_proc == pcpui->owning_proc);
	copy_current_ctx_to(&env->scp_ctx);

	/* Make the new process have the same VMRs as the older.  This will copy the
	 * contents of non MAP_SHARED pages to the new VMRs. */
	if (duplicate_vmrs(e, env)) {
		proc_destroy(env);	/* this is prob what you want, not decref by 2 */
		proc_decref(env);
		set_errno(ENOMEM);
		return -1;
	}
	/* Switch to the new proc's address space and finish the syscall.  We'll
	 * never naturally finish this syscall for the new proc, since its memory
	 * is cloned before we return for the original process.  If we ever do CoW
	 * for forked memory, this will be the first place that gets CoW'd. */
	temp = switch_to(env);
	finish_current_sysc(0);
	switch_back(env, temp);

	/* Copy some state from the original proc into the new proc. */
	env->env_flags = e->env_flags;

	inherit_strace(e, env);

	/* In general, a forked process should be a fresh process, and we copy over
	 * whatever stuff is needed between procinfo/procdata. */
	*env->procdata = *e->procdata;
	env->procinfo->program_end = e->procinfo->program_end;

	/* FYI: once we call ready, the proc is open for concurrent usage */
	__proc_ready(env);
	proc_wakeup(env);

	// don't decref the new process.
	// that will happen when the parent waits for it.
	// TODO: if the parent doesn't wait, we need to change the child's parent
	// when the parent dies, or at least decref it

	printd("[PID %d] fork PID %d\n", e->pid, env->pid);
	ret = env->pid;
	profiler_notify_new_process(env);
	proc_decref(env);	/* give up the reference created in proc_alloc() */
	return ret;
}

/* string for sys_exec arguments. Assumes that d is pointing to zero'd
 * storage or storage that does not require null termination or
 * provides the null. */
static int execargs_stringer(struct proc *p, char *d, size_t slen,
			     char *path, size_t path_l,
			     char *argenv, size_t argenv_l)
{
	int argc, envc, i;
	char **argv, **envp;
	struct argenv *kargenv;
	int amt;
	char *s = d;
	char *e = d + slen;

	if (path_l > slen)
		path_l = slen;
	if (memcpy_from_user(p, d, path, path_l)) {
		s = seprintf(s, e, "Invalid exec path");
		return s - d;
	}
	s += path_l;

	/* yes, this code is cloned from below. I wrote a helper but
	 * Barret and I concluded after talking about it that the
	 * helper was not really helper-ful, as it has almost 10
	 * arguments. Please, don't suggest a cpp macro. Thank you. */
	/* Check the size of the argenv array, error out if too large. */
	if ((argenv_l < sizeof(struct argenv)) || (argenv_l > ARG_MAX)) {
		s = seprintf(s, e, "The argenv array has an invalid size: %lu\n",
				  argenv_l);
		return s - d;
	}
	/* Copy the argenv array into a kernel buffer. */
	kargenv = user_memdup_errno(p, argenv, argenv_l);
	if (!kargenv) {
		s = seprintf(s, e, "Failed to copy in the args and environment");
		return s - d;
	}
	/* Unpack the argenv array into more usable variables. Integrity checking
	 * done along side this as well. */
	if (unpack_argenv(kargenv, argenv_l, &argc, &argv, &envc, &envp)) {
		s = seprintf(s, e, "Failed to unpack the args");
		user_memdup_free(p, kargenv);
		return s - d;
	}
	s = seprintf(s, e, "[%d]{", argc);
	for (i = 0; i < argc; i++)
		s = seprintf(s, e, "%s, ", argv[i]);
	s = seprintf(s, e, "}");

	user_memdup_free(p, kargenv);
	return s - d;
}

/* Load the binary "path" into the current process, and start executing it.
 * argv and envp are magically bundled in procinfo for now.  Keep in sync with
 * glibc's sysdeps/ros/execve.c.  Once past a certain point, this function won't
 * return.  It assumes (and checks) that it is current.  Don't give it an extra
 * refcnt'd *p (syscall won't do that).
 * Note: if someone batched syscalls with this call, they could clobber their
 * old memory (and will likely PF and die).  Don't do it... */
static int sys_exec(struct proc *p, char *path, size_t path_l,
                    char *argenv, size_t argenv_l)
{
	int ret = -1;
	char *t_path = NULL;
	struct file *program;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	int argc, envc;
	char **argv, **envp;
	struct argenv *kargenv;

	/* We probably want it to never be allowed to exec if it ever was _M */
	if (p->state != PROC_RUNNING_S) {
		set_errno(EINVAL);
		return -1;
	}
	if (p != pcpui->cur_proc) {
		set_errno(EINVAL);
		return -1;
	}

	/* Can't exec if we don't have a current_ctx to restart (if we fail).  This
	 * isn't 100% true, but I'm okay with it. */
	if (!pcpui->cur_ctx) {
		set_errno(EINVAL);
		return -1;
	}
	/* Preemptively copy out the cur_ctx, in case we fail later (easier on
	 * cur_ctx if we do this now) */
	assert(pcpui->cur_proc == pcpui->owning_proc);
	copy_current_ctx_to(&p->scp_ctx);
	/* Check the size of the argenv array, error out if too large. */
	if ((argenv_l < sizeof(struct argenv)) || (argenv_l > ARG_MAX)) {
		set_error(EINVAL, "The argenv array has an invalid size: %lu\n",
				  argenv_l);
		return -1;
	}
	/* Copy the argenv array into a kernel buffer. */
	kargenv = user_memdup_errno(p, argenv, argenv_l);
	if (!kargenv) {
		set_errstr("Failed to copy in the args and environment");
		return -1;
	}
	/* Unpack the argenv array into more usable variables. Integrity checking
	 * done along side this as well. */
	if (unpack_argenv(kargenv, argenv_l, &argc, &argv, &envc, &envp)) {
		user_memdup_free(p, kargenv);
		set_error(EINVAL, "Failed to unpack the args");
		return -1;
	}
	t_path = copy_in_path(p, path, path_l);
	if (!t_path) {
		user_memdup_free(p, kargenv);
		return -1;
	}
	/* This could block: */
	/* TODO: 9ns support */
	program = do_file_open(t_path, O_READ, 0);
	/* Clear the current_ctx.  We won't be returning the 'normal' way.  Even if
	 * we want to return with an error, we need to go back differently in case
	 * we succeed.  This needs to be done before we could possibly block, but
	 * unfortunately happens before the point of no return.
	 *
	 * Note that we will 'hard block' if we block at all.  We can't return to
	 * userspace and then asynchronously finish the exec later. */
	clear_owning_proc(core_id());
	if (!program)
		goto early_error;
	if (!is_valid_elf(program)) {
		set_errno(ENOEXEC);
		goto mid_error;
	}
	/* This is the point of no return for the process. */
	/* progname is argv0, which accounts for symlinks */
	proc_replace_binary_path(p, t_path);
	proc_set_progname(p, argc ? argv[0] : NULL);
	proc_init_procdata(p);
	p->procinfo->program_end = 0;
	/* When we destroy our memory regions, accessing cur_sysc would PF */
	pcpui->cur_kthread->sysc = 0;
	unmap_and_destroy_vmrs(p);
	/* close the CLOEXEC ones */
	close_fdt(&p->open_files, TRUE);
	env_user_mem_free(p, 0, UMAPTOP);
	if (load_elf(p, program, argc, argv, envc, envp)) {
		kref_put(&program->f_kref);
		user_memdup_free(p, kargenv);
		/* Note this is an inedible reference, but proc_destroy now returns */
		proc_destroy(p);
		/* We don't want to do anything else - we just need to not accidentally
		 * return to the user (hence the all_out) */
		goto all_out;
	}
	printd("[PID %d] exec %s\n", p->pid, file_name(program));
	kref_put(&program->f_kref);
	systrace_finish_trace(pcpui->cur_kthread, 0);
	goto success;
	/* These error and out paths are so we can handle the async interface, both
	 * for when we want to error/return to the proc, as well as when we succeed
	 * and want to start the newly exec'd _S */
mid_error:
	/* These two error paths are for when we want to restart the process with an
	 * error value (errno is already set). */
	kref_put(&program->f_kref);
early_error:
	free_path(p, t_path);
	finish_current_sysc(-1);
	systrace_finish_trace(pcpui->cur_kthread, -1);
success:
	user_memdup_free(p, kargenv);
	free_sysc_str(pcpui->cur_kthread);
	/* Here's how we restart the new (on success) or old (on failure) proc: */
	spin_lock(&p->proc_lock);
	__seq_start_write(&p->procinfo->coremap_seqctr);
	__unmap_vcore(p, 0);
	__seq_end_write(&p->procinfo->coremap_seqctr);
	__proc_set_state(p, PROC_WAITING);	/* fake a yield */
	spin_unlock(&p->proc_lock);
	proc_wakeup(p);
all_out:
	/* we can't return, since we'd write retvals to the old location of the
	 * syscall struct (which has been freed and is in the old userspace) (or has
	 * already been written to).*/
	disable_irq();			/* abandon_core/clear_own wants irqs disabled */
	abandon_core();
	smp_idle();				/* will reenable interrupts */
}

/* Helper, will attempt a particular wait on a proc.  Returns the pid of the
 * process if we waited on it successfully, and the status will be passed back
 * in ret_status (kernel memory).  Returns 0 if the wait failed and we should
 * try again.  Returns -1 if we should abort.  Only handles DYING.  Callers
 * need to lock to protect the children tailq and reaping bits. */
static pid_t try_wait(struct proc *parent, struct proc *child, int *ret_status,
                      int options)
{
	if (proc_is_dying(child)) {
		/* Disown returns -1 if it's already been disowned or we should o/w
		 * abort.  This can happen if we have concurrent waiters, both with
		 * pointers to the child (only one should reap).  Note that if we don't
		 * do this, we could go to sleep and never receive a cv_signal. */
		if (__proc_disown_child(parent, child))
			return -1;
		/* despite disowning, the child won't be freed til we drop this ref
		 * held by this function, so it is safe to access the memory.
		 *
		 * Note the exit code one byte in the 0xff00 spot.  Check out glibc's
		 * posix/sys/wait.h and bits/waitstatus.h for more info.  If we ever
		 * deal with signalling and stopping, we'll need to do some more work
		 * here.*/
		*ret_status = (child->exitcode & 0xff) << 8;
		return child->pid;
	}
	return 0;
}

/* Helper, like try_wait, but attempts a wait on any of the children, returning
 * the specific PID we waited on, 0 to try again (a waitable exists), and -1 to
 * abort (no children/waitables exist).  Callers need to lock to protect the
 * children tailq and reaping bits.*/
static pid_t try_wait_any(struct proc *parent, int *ret_status, int options)
{
	struct proc *i, *temp;
	pid_t retval;
	if (TAILQ_EMPTY(&parent->children))
		return -1;
	/* Could have concurrent waiters mucking with the tailq, caller must lock */
	TAILQ_FOREACH_SAFE(i, &parent->children, sibling_link, temp) {
		retval = try_wait(parent, i, ret_status, options);
		/* This catches a thread causing a wait to fail but not taking the
		 * child off the list before unlocking.  Should never happen. */
		assert(retval != -1);
		/* Succeeded, return the pid of the child we waited on */
		if (retval)
			return retval;
	}
	assert(retval == 0);
	return 0;
}

/* Waits on a particular child, returns the pid of the child waited on, and
 * puts the ret status in *ret_status.  Returns the pid if we succeeded, 0 if
 * the child was not waitable and WNOHANG, and -1 on error. */
static pid_t wait_one(struct proc *parent, struct proc *child, int *ret_status,
                      int options)
{
	pid_t retval;
	cv_lock(&parent->child_wait);
	/* retval == 0 means we should block */
	retval = try_wait(parent, child, ret_status, options);
	if ((retval == 0) && (options & WNOHANG))
		goto out_unlock;
	while (!retval) {
		cpu_relax();
		cv_wait(&parent->child_wait);
		/* If we're dying, then we don't need to worry about waiting.  We don't
		 * do this yet, but we'll need this outlet when we deal with orphaned
		 * children and having init inherit them. */
		if (proc_is_dying(parent))
			goto out_unlock;
		/* Any child can wake us up, but we check for the particular child we
		 * care about */
		retval = try_wait(parent, child, ret_status, options);
	}
	if (retval == -1) {
		/* Child was already waited on by a concurrent syscall. */
		set_errno(ECHILD);
	}
	/* Fallthrough */
out_unlock:
	cv_unlock(&parent->child_wait);
	return retval;
}

/* Waits on any child, returns the pid of the child waited on, and puts the ret
 * status in *ret_status.  Is basically a waitpid(-1, ... );  See wait_one for
 * more details.  Returns -1 if there are no children to wait on, and returns 0
 * if there are children and we need to block but WNOHANG was set. */
static pid_t wait_any(struct proc *parent, int *ret_status, int options)
{
	pid_t retval;
	cv_lock(&parent->child_wait);
	retval = try_wait_any(parent, ret_status, options);
	if ((retval == 0) && (options & WNOHANG))
		goto out_unlock;
	while (!retval) {
		cpu_relax();
		cv_wait(&parent->child_wait);
		if (proc_is_dying(parent))
			goto out_unlock;
		/* Any child can wake us up from the CV.  This is a linear try_wait
		 * scan.  If we have a lot of children, we could optimize this. */
		retval = try_wait_any(parent, ret_status, options);
	}
	if (retval == -1)
		assert(TAILQ_EMPTY(&parent->children));
	/* Fallthrough */
out_unlock:
	cv_unlock(&parent->child_wait);
	return retval;
}

/* Note: we only allow waiting on children (no such thing as threads, for
 * instance).  Right now we only allow waiting on termination (not signals),
 * and we don't have a way for parents to disown their children (such as
 * ignoring SIGCHLD, see man 2 waitpid's Notes).
 *
 * We don't bother with stop/start signals here, though we can probably build
 * it in the helper above.
 *
 * Returns the pid of who we waited on, or -1 on error, or 0 if we couldn't
 * wait (WNOHANG). */
static pid_t sys_waitpid(struct proc *parent, pid_t pid, int *status,
                         int options)
{
	struct proc *child;
	pid_t retval = 0;
	int ret_status = 0;

	sysc_save_str("waitpid on %d", pid);
	/* -1 is the signal for 'any child' */
	if (pid == -1) {
		retval = wait_any(parent, &ret_status, options);
		goto out;
	}
	child = pid2proc(pid);
	if (!child) {
		set_errno(ECHILD);	/* ECHILD also used for no proc */
		retval = -1;
		goto out;
	}
	if (!(parent->pid == child->ppid)) {
		set_errno(ECHILD);
		retval = -1;
		goto out_decref;
	}
	retval = wait_one(parent, child, &ret_status, options);
	/* fall-through */
out_decref:
	proc_decref(child);
out:
	/* ignoring / don't care about memcpy's retval here. */
	if (status)
		memcpy_to_user(parent, status, &ret_status, sizeof(ret_status));
	printd("[PID %d] waited for PID %d, got retval %d (status 0x%x)\n",
	       parent->pid, pid, retval, ret_status);
	return retval;
}

/************** Memory Management Syscalls **************/

static void *sys_mmap(struct proc *p, uintptr_t addr, size_t len, int prot,
                      int flags, int fd, off_t offset)
{
	return mmap(p, addr, len, prot, flags, fd, offset);
}

static intreg_t sys_mprotect(struct proc *p, void *addr, size_t len, int prot)
{
	return mprotect(p, (uintptr_t)addr, len, prot);
}

static intreg_t sys_munmap(struct proc *p, void *addr, size_t len)
{
	return munmap(p, (uintptr_t)addr, len);
}

static ssize_t sys_shared_page_alloc(env_t* p1,
                                     void **_addr, pid_t p2_id,
                                     int p1_flags, int p2_flags
                                    )
{
	printk("[kernel] shared page alloc is deprecated/unimplemented.\n");
	return -1;
}

static int sys_shared_page_free(env_t* p1, void *addr, pid_t p2)
{
	return -1;
}

/* Helper, to do the actual provisioning of a resource to a proc */
static int prov_resource(struct proc *target, unsigned int res_type,
                         long res_val)
{
	switch (res_type) {
		case (RES_CORES):
			/* in the off chance we have a kernel scheduler that can't
			 * provision, we'll need to change this. */
			return provision_core(target, res_val);
		default:
			printk("[kernel] received provisioning for unknown resource %d\n",
			       res_type);
			set_errno(ENOENT);	/* or EINVAL? */
			return -1;
	}
}

/* Rough syscall to provision res_val of type res_type to target_pid */
static int sys_provision(struct proc *p, int target_pid,
                         unsigned int res_type, long res_val)
{
	struct proc *target = pid2proc(target_pid);
	int retval;
	if (!target) {
		if (target_pid == 0)
			return prov_resource(0, res_type, res_val);
		/* debugging interface */
		if (target_pid == -1)
			print_coreprov_map();
		set_errno(ESRCH);
		return -1;
	}
	retval = prov_resource(target, res_type, res_val);
	proc_decref(target);
	return retval;
}

/* Untested.  Will notify the target on the given vcore, if the caller controls
 * the target.  Will honor the target's wanted/vcoreid.  u_ne can be NULL. */
static int sys_notify(struct proc *p, int target_pid, unsigned int ev_type,
                      struct event_msg *u_msg)
{
	struct event_msg local_msg = {0};
	struct proc *target = get_controllable_proc(p, target_pid);
	if (!target)
		return -1;
	/* if the user provided an ev_msg, copy it in and use that */
	if (u_msg) {
		if (memcpy_from_user(p, &local_msg, u_msg, sizeof(struct event_msg))) {
			proc_decref(target);
			set_errno(EINVAL);
			return -1;
		}
	} else {
		local_msg.ev_type = ev_type;
	}
	send_kernel_event(target, &local_msg, 0);
	proc_decref(target);
	return 0;
}

/* Will notify the calling process on the given vcore, independently of WANTED
 * or advertised vcoreid.  If you change the parameters, change pop_user_ctx().
 */
static int sys_self_notify(struct proc *p, uint32_t vcoreid,
                           unsigned int ev_type, struct event_msg *u_msg,
                           bool priv)
{
	struct event_msg local_msg = {0};
	/* if the user provided an ev_msg, copy it in and use that */
	if (u_msg) {
		if (memcpy_from_user(p, &local_msg, u_msg, sizeof(struct event_msg))) {
			set_errno(EINVAL);
			return -1;
		}
	} else {
		local_msg.ev_type = ev_type;
	}
	if (local_msg.ev_type >= MAX_NR_EVENT) {
		printk("[kernel] received self-notify for vcoreid %d, ev_type %d, "
		       "u_msg %p, u_msg->type %d\n", vcoreid, ev_type, u_msg,
		       u_msg ? u_msg->ev_type : 0);
		return -1;
	}
	/* this will post a message and IPI, regardless of wants/needs/debutantes.*/
	post_vcore_event(p, &local_msg, vcoreid, priv ? EVENT_VCORE_PRIVATE : 0);
	proc_notify(p, vcoreid);
	return 0;
}

static int sys_send_event(struct proc *p, struct event_queue *ev_q,
                          struct event_msg *u_msg, uint32_t vcoreid)
{
	struct event_msg local_msg = {0};

	if (memcpy_from_user(p, &local_msg, u_msg, sizeof(struct event_msg))) {
		set_errno(EINVAL);
		return -1;
	}
	send_event(p, ev_q, &local_msg, vcoreid);
	return 0;
}

/* Puts the calling core into vcore context, if it wasn't already, via a
 * self-IPI / active notification.  Barring any weird unmappings, we just send
 * ourselves a __notify. */
static int sys_vc_entry(struct proc *p)
{
	send_kernel_message(core_id(), __notify, (long)p, 0, 0, KMSG_ROUTINE);
	return 0;
}

/* This will halt the core, waking on an IRQ.  These could be kernel IRQs for
 * things like timers or devices, or they could be IPIs for RKMs (__notify for
 * an evq with IPIs for a syscall completion, etc).
 *
 * We don't need to finish the syscall early (worried about the syscall struct,
 * on the vcore's stack).  The syscall will finish before any __preempt RKM
 * executes, so the vcore will not restart somewhere else before the syscall
 * completes (unlike with yield, where the syscall itself adjusts the vcore
 * structures).
 *
 * In the future, RKM code might avoid sending IPIs if the core is already in
 * the kernel.  That code will need to check the CPU's state in some manner, and
 * send if the core is halted/idle.
 *
 * The core must wake up for RKMs, including RKMs that arrive while the kernel
 * is trying to halt.  The core need not abort the halt for notif_pending for
 * the vcore, only for a __notify or other RKM.  Anyone setting notif_pending
 * should then attempt to __notify (o/w it's probably a bug). */
static int sys_halt_core(struct proc *p, unsigned long usec)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct preempt_data *vcpd;
	/* The user can only halt CG cores!  (ones it owns) */
	if (management_core())
		return -1;
	disable_irq();
	/* both for accounting and possible RKM optimizations */
	__set_cpu_state(pcpui, CPU_STATE_IDLE);
	wrmb();
	if (has_routine_kmsg()) {
		__set_cpu_state(pcpui, CPU_STATE_KERNEL);
		enable_irq();
		return 0;
	}
	/* This situation possible, though the check is not necessary.  We can't
	 * assert notif_pending isn't set, since another core may be in the
	 * proc_notify.  Thus we can't tell if this check here caught a bug, or just
	 * aborted early. */
	vcpd = &p->procdata->vcore_preempt_data[pcpui->owning_vcoreid];
	if (vcpd->notif_pending) {
		__set_cpu_state(pcpui, CPU_STATE_KERNEL);
		enable_irq();
		return 0;
	}
	/* CPU_STATE is reset to KERNEL by the IRQ handler that wakes us */
	cpu_halt();
	return 0;
}

/* Changes a process into _M mode, or -EINVAL if it already is an mcp.
 * __proc_change_to_m() returns and we'll eventually finish the sysc later.  The
 * original context may restart on a remote core before we return and finish,
 * but that's fine thanks to the async kernel interface. */
static int sys_change_to_m(struct proc *p)
{
	int retval = proc_change_to_m(p);
	/* convert the kernel error code into (-1, errno) */
	if (retval) {
		set_errno(-retval);
		retval = -1;
	}
	return retval;
}

/* Assists the user/2LS by atomically running *ctx and leaving vcore context.
 * Normally, the user can do this themselves, but x86 VM contexts need kernel
 * support.  The caller ought to be in vcore context, and if a notif is pending,
 * then the calling vcore will restart in a fresh VC ctx (as if it was notified
 * or did a sys_vc_entry).
 *
 * Note that this will set the TLS too, which is part of the context.  Parlib's
 * pop_user_ctx currently does *not* do this, since the TLS is managed
 * separately.  If you want to use this syscall for testing, you'll need to 0
 * out fsbase and conditionally write_msr in proc_pop_ctx(). */
static int sys_pop_ctx(struct proc *p, struct user_context *ctx)
{
	int pcoreid = core_id();
	struct per_cpu_info *pcpui = &per_cpu_info[pcoreid];
	int vcoreid = pcpui->owning_vcoreid;
	struct preempt_data *vcpd = &p->procdata->vcore_preempt_data[vcoreid];

	/* With change_to, there's a bunch of concerns about changing the vcore map,
	 * since the kernel may have already locked and sent preempts, deaths, etc.
	 *
	 * In this case, we don't care as much.  Other than notif_pending and
	 * notif_disabled, it's more like we're just changing a few registers in
	 * cur_ctx.  We can safely order-after any kernel messages or other changes,
	 * as if the user had done all of the changes we'll make and then did a
	 * no-op syscall.
	 *
	 * Since we are mucking with current_ctx, it is important that we don't
	 * block before or during this syscall. */
	arch_finalize_ctx(pcpui->cur_ctx);
	if (copy_from_user(pcpui->cur_ctx, ctx, sizeof(struct user_context))) {
		/* The 2LS isn't really in a position to handle errors.  At the very
		 * least, we can print something and give them a fresh vc ctx. */
		printk("[kernel] unable to copy user_ctx, 2LS bug\n");
		memset(pcpui->cur_ctx, 0, sizeof(struct user_context));
		proc_init_ctx(pcpui->cur_ctx, vcoreid, vcpd->vcore_entry,
		              vcpd->vcore_stack, vcpd->vcore_tls_desc);
		return -1;
	}
	proc_secure_ctx(pcpui->cur_ctx);
	/* The caller leaves vcore context no matter what.  We'll put them back in
	 * if they missed a message. */
	vcpd->notif_disabled = FALSE;
	wrmb();	/* order disabled write before pending read */
	if (vcpd->notif_pending)
		send_kernel_message(pcoreid, __notify, (long)p, 0, 0, KMSG_ROUTINE);
	return 0;
}

/* Initializes a process to run virtual machine contexts, returning the number
 * initialized, optionally setting errno */
static int sys_vmm_setup(struct proc *p, unsigned int nr_guest_pcores,
                         struct vmm_gpcore_init *gpcis, int flags)
{
	int ret;
	ERRSTACK(1);

	if (waserror()) {
		poperror();
		return -1;
	}
	ret = vmm_struct_init(p, nr_guest_pcores, gpcis, flags);
	poperror();
	return ret;
}

static int sys_vmm_poke_guest(struct proc *p, int guest_pcoreid)
{
	return vmm_poke_guest(p, guest_pcoreid);
}

/* Pokes the ksched for the given resource for target_pid.  If the target pid
 * == 0, we just poke for the calling process.  The common case is poking for
 * self, so we avoid the lookup.
 *
 * Not sure if you could harm someone via asking the kernel to look at them, so
 * we'll do a 'controls' check for now.  In the future, we might have something
 * in the ksched that limits or penalizes excessive pokes. */
static int sys_poke_ksched(struct proc *p, int target_pid,
                           unsigned int res_type)
{
	struct proc *target;
	int retval = 0;
	if (!target_pid) {
		poke_ksched(p, res_type);
		return 0;
	}
	target = pid2proc(target_pid);
	if (!target) {
		set_errno(ESRCH);
		return -1;
	}
	if (!proc_controls(p, target)) {
		set_errno(EPERM);
		retval = -1;
		goto out;
	}
	poke_ksched(target, res_type);
out:
	proc_decref(target);
	return retval;
}

static int sys_abort_sysc(struct proc *p, struct syscall *sysc)
{
	return abort_sysc(p, sysc);
}

static int sys_abort_sysc_fd(struct proc *p, int fd)
{
	/* Consider checking for a bad fd.  Doesn't matter now, since we only look
	 * for actual syscalls blocked that had used fd. */
	return abort_all_sysc_fd(p, fd);
}

static unsigned long sys_populate_va(struct proc *p, uintptr_t va,
                                     unsigned long nr_pgs)
{
	return populate_va(p, ROUNDDOWN(va, PGSIZE), nr_pgs);
}

static intreg_t sys_read(struct proc *p, int fd, void *buf, size_t len)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	ssize_t ret;
	struct file *file = get_file_from_fd(&p->open_files, fd);
	sysc_save_str("read on fd %d", fd);
	/* VFS */
	if (file) {
		if (!file->f_op->read) {
			kref_put(&file->f_kref);
			set_errno(EINVAL);
			return -1;
		}
		/* TODO: (UMEM) currently, read() handles user memcpy
		 * issues, but we probably should user_mem_check and
		 * pin the region here, so read doesn't worry about
		 * it */
		ret = file->f_op->read(file, buf, len, &file->f_pos);
		kref_put(&file->f_kref);
	} else {
		/* plan9, should also handle errors (EBADF) */
		ret = sysread(fd, buf, len);
	}
	return ret;
}

static intreg_t sys_write(struct proc *p, int fd, const void *buf, size_t len)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	ssize_t ret;
	struct file *file = get_file_from_fd(&p->open_files, fd);

	sysc_save_str("write on fd %d", fd);
	/* VFS */
	if (file) {
		if (!file->f_op->write) {
			kref_put(&file->f_kref);
			set_errno(EINVAL);
			return -1;
		}
		/* TODO: (UMEM) */
		ret = file->f_op->write(file, buf, len, &file->f_pos);
		kref_put(&file->f_kref);
	} else {
		/* plan9, should also handle errors */
		ret = syswrite(fd, (void*)buf, len);
	}
	return ret;
}

/* Checks args/reads in the path, opens the file (relative to fromfd if the path
 * is not absolute), and inserts it into the process's open file list. */
static intreg_t sys_openat(struct proc *p, int fromfd, const char *path,
                           size_t path_l, int oflag, int mode)
{
	int fd = -1;
	struct file *file = 0;
	char *t_path;

	printd("File %s Open attempt oflag %x mode %x\n", path, oflag, mode);
	if ((oflag & O_PATH) && (oflag & O_ACCMODE)) {
		set_error(EINVAL, "Cannot open O_PATH with any I/O perms (O%o)", oflag);
		return -1;
	}
	t_path = copy_in_path(p, path, path_l);
	if (!t_path)
		return -1;
	sysc_save_str("open %s at fd %d", t_path, fromfd);
	mode &= ~p->fs_env.umask;
	/* Only check the VFS for legacy opens.  It doesn't support openat.  Actual
	 * openats won't check here, and file == 0. */
	if ((t_path[0] == '/') || (fromfd == AT_FDCWD))
		file = do_file_open(t_path, oflag, mode);
	else
		set_errno(ENOENT);	/* was not in the VFS. */
	if (file) {
		/* VFS lookup succeeded */
		/* stores the ref to file */
		fd = insert_file(&p->open_files, file, 0, FALSE, oflag & O_CLOEXEC);
		kref_put(&file->f_kref);	/* drop our ref */
		if (fd < 0)
			warn("File insertion failed");
	} else if (get_errno() == ENOENT) {
		/* VFS failed due to ENOENT.  Other errors don't fall back to 9ns */
		unset_errno();	/* Go can't handle extra errnos */
		fd = sysopenat(fromfd, t_path, oflag);
		/* successful lookup with CREATE and EXCL is an error */
		if (fd != -1) {
			if ((oflag & O_CREATE) && (oflag & O_EXCL)) {
				set_errno(EEXIST);
				sysclose(fd);
				free_path(p, t_path);
				return -1;
			}
		} else {
			if (oflag & O_CREATE) {
				mode &= S_PMASK;
				fd = syscreate(t_path, oflag, mode);
			}
		}
	}
	free_path(p, t_path);
	printd("File %s Open, fd=%d\n", path, fd);
	return fd;
}

static intreg_t sys_close(struct proc *p, int fd)
{
	struct file *file = get_file_from_fd(&p->open_files, fd);
	int retval = 0;
	printd("sys_close %d\n", fd);
	/* VFS */
	if (file) {
		put_file_from_fd(&p->open_files, fd);
		kref_put(&file->f_kref);	/* Drop the ref from get_file */
		return 0;
	}
	/* 9ns, should also handle errors (bad FD, etc) */
	retval = sysclose(fd);
	return retval;
}

/* kept around til we remove the last ufe */
#define ufe(which,a0,a1,a2,a3) \
	frontend_syscall_errno(p,APPSERVER_SYSCALL_##which,\
	                   (int)(a0),(int)(a1),(int)(a2),(int)(a3))

static intreg_t sys_fstat(struct proc *p, int fd, struct kstat *u_stat)
{
	struct kstat *kbuf;
	struct file *file;
	kbuf = kmalloc(sizeof(struct kstat), 0);
	if (!kbuf) {
		set_errno(ENOMEM);
		return -1;
	}
	file = get_file_from_fd(&p->open_files, fd);
	/* VFS */
	if (file) {
		stat_inode(file->f_dentry->d_inode, kbuf);
		kref_put(&file->f_kref);
	} else {
		unset_errno();	/* Go can't handle extra errnos */
	    if (sysfstatakaros(fd, (struct kstat *)kbuf) < 0) {
			kfree(kbuf);
			return -1;
		}
	}
	/* TODO: UMEM: pin the memory, copy directly, and skip the kernel buffer */
	if (memcpy_to_user_errno(p, u_stat, kbuf, sizeof(struct kstat))) {
		kfree(kbuf);
		return -1;
	}
	kfree(kbuf);
	return 0;
}

/* sys_stat() and sys_lstat() do nearly the same thing, differing in how they
 * treat a symlink for the final item, which (probably) will be controlled by
 * the lookup flags */
static intreg_t stat_helper(struct proc *p, const char *path, size_t path_l,
                            struct kstat *u_stat, int flags)
{
	struct kstat *kbuf;
	struct dentry *path_d;
	char *t_path = copy_in_path(p, path, path_l);
	int retval = 0;
	if (!t_path)
		return -1;
	kbuf = kmalloc(sizeof(struct kstat), 0);
	if (!kbuf) {
		set_errno(ENOMEM);
		retval = -1;
		goto out_with_path;
	}
	/* Check VFS for path */
	path_d = lookup_dentry(t_path, flags);
	if (path_d) {
		stat_inode(path_d->d_inode, kbuf);
		kref_put(&path_d->d_kref);
	} else {
		/* VFS failed, checking 9ns */
		unset_errno();	/* Go can't handle extra errnos */
		retval = sysstatakaros(t_path, (struct stat *)kbuf);
		printd("sysstat returns %d\n", retval);
		/* both VFS and 9ns failed, bail out */
		if (retval < 0)
			goto out_with_kbuf;
	}
	/* TODO: UMEM: pin the memory, copy directly, and skip the kernel buffer */
	if (memcpy_to_user_errno(p, u_stat, kbuf, sizeof(struct kstat)))
		retval = -1;
	/* Fall-through */
out_with_kbuf:
	kfree(kbuf);
out_with_path:
	free_path(p, t_path);
	return retval;
}

/* Follow a final symlink */
static intreg_t sys_stat(struct proc *p, const char *path, size_t path_l,
                         struct kstat *u_stat)
{
	return stat_helper(p, path, path_l, u_stat, LOOKUP_FOLLOW);
}

/* Don't follow a final symlink */
static intreg_t sys_lstat(struct proc *p, const char *path, size_t path_l,
                          struct kstat *u_stat)
{
	return stat_helper(p, path, path_l, u_stat, 0);
}

intreg_t sys_fcntl(struct proc *p, int fd, int cmd, unsigned long arg1,
                   unsigned long arg2, unsigned long arg3, unsigned long arg4)
{
	int retval = 0;
	int newfd;
	struct file *file = get_file_from_fd(&p->open_files, fd);

	if (!file) {
		/* 9ns hack */
		switch (cmd) {
			case (F_DUPFD):
				return sysdup(fd);
			case (F_GETFD):
			case (F_SETFD):
			case (F_SYNC):
			case (F_ADVISE):
				/* TODO: 9ns versions */
				return 0;
			case (F_GETFL):
				return fd_getfl(fd);
			case (F_SETFL):
				return fd_setfl(fd, arg1);
			default:
				warn("Unsupported fcntl cmd %d\n", cmd);
		}
		/* not really ever calling this, even for badf, due to the switch */
		set_errno(EBADF);
		return -1;
	}

	/* TODO: these are racy */
	switch (cmd) {
		case (F_DUPFD):
			retval = insert_file(&p->open_files, file, arg1, FALSE, FALSE);
			if (retval < 0) {
				set_errno(-retval);
				retval = -1;
			}
			break;
		case (F_GETFD):
			retval = p->open_files.fd[fd].fd_flags;
			break;
		case (F_SETFD):
			/* I'm considering not supporting this at all.  They must do it at
			 * open time or fix their buggy/racy code. */
			spin_lock(&p->open_files.lock);
			if (arg1 & FD_CLOEXEC)
				p->open_files.fd[fd].fd_flags |= FD_CLOEXEC;
			retval = p->open_files.fd[fd].fd_flags;
			spin_unlock(&p->open_files.lock);
			break;
		case (F_GETFL):
			retval = file->f_flags;
			break;
		case (F_SETFL):
			/* only allowed to set certain flags. */
			arg1 &= O_FCNTL_SET_FLAGS;
			file->f_flags = (file->f_flags & ~O_FCNTL_SET_FLAGS) | arg1;
			break;
		case (F_SYNC):
			/* TODO (if we keep the VFS) */
			retval = 0;
			break;
		case (F_ADVISE):
			/* TODO  (if we keep the VFS)*/
			retval = 0;
			break;
		default:
			warn("Unsupported fcntl cmd %d\n", cmd);
	}
	kref_put(&file->f_kref);
	return retval;
}

static intreg_t sys_access(struct proc *p, const char *path, size_t path_l,
                           int mode)
{
	int retval;
	char *t_path = copy_in_path(p, path, path_l);
	if (!t_path)
		return -1;
	/* TODO: 9ns support */
	retval = do_access(t_path, mode);
	free_path(p, t_path);
	printd("Access for path: %s retval: %d\n", path, retval);
	if (retval < 0) {
		set_errno(-retval);
		return -1;
	}
	return retval;
}

intreg_t sys_umask(struct proc *p, int mask)
{
	int old_mask = p->fs_env.umask;
	p->fs_env.umask = mask & S_PMASK;
	return old_mask;
}

/* 64 bit seek, with the off64_t passed in via two (potentially 32 bit) off_ts.
 * We're supporting both 32 and 64 bit kernels/userspaces, but both use the
 * llseek syscall with 64 bit parameters. */
static intreg_t sys_llseek(struct proc *p, int fd, off_t offset_hi,
                           off_t offset_lo, off64_t *result, int whence)
{
	off64_t retoff = 0;
	off64_t tempoff = 0;
	int ret = 0;
	struct file *file;
	tempoff = offset_hi;
	tempoff <<= 32;
	tempoff |= offset_lo;
	file = get_file_from_fd(&p->open_files, fd);
	if (file) {
		ret = file->f_op->llseek(file, tempoff, &retoff, whence);
		kref_put(&file->f_kref);
	} else {
		retoff = sysseek(fd, tempoff, whence);
		ret = (retoff < 0);
	}

	if (ret)
		return -1;
	if (memcpy_to_user_errno(p, result, &retoff, sizeof(off64_t)))
		return -1;
	return 0;
}

intreg_t sys_link(struct proc *p, char *old_path, size_t old_l,
                  char *new_path, size_t new_l)
{
	int ret;
	char *t_oldpath = copy_in_path(p, old_path, old_l);
	if (t_oldpath == NULL)
		return -1;
	char *t_newpath = copy_in_path(p, new_path, new_l);
	if (t_newpath == NULL) {
		free_path(p, t_oldpath);
		return -1;
	}
	ret = do_link(t_oldpath, t_newpath);
	free_path(p, t_oldpath);
	free_path(p, t_newpath);
	return ret;
}

intreg_t sys_unlink(struct proc *p, const char *path, size_t path_l)
{
	int retval;
	char *t_path = copy_in_path(p, path, path_l);
	if (!t_path)
		return -1;
	retval = do_unlink(t_path);
	if (retval && (get_errno() == ENOENT)) {
		unset_errno();
		retval = sysremove(t_path);
	}
	free_path(p, t_path);
	return retval;
}

intreg_t sys_symlink(struct proc *p, char *old_path, size_t old_l,
                     char *new_path, size_t new_l)
{
	int ret;
	char *t_oldpath = copy_in_path(p, old_path, old_l);
	if (t_oldpath == NULL)
		return -1;
	char *t_newpath = copy_in_path(p, new_path, new_l);
	if (t_newpath == NULL) {
		free_path(p, t_oldpath);
		return -1;
	}
	ret = do_symlink(t_newpath, t_oldpath, S_IRWXU | S_IRWXG | S_IRWXO);
	free_path(p, t_oldpath);
	free_path(p, t_newpath);
	return ret;
}

intreg_t sys_readlink(struct proc *p, char *path, size_t path_l,
                      char *u_buf, size_t buf_l)
{
	char *symname = NULL;
	uint8_t *buf = NULL;
	ssize_t copy_amt;
	int ret = -1;
	struct dentry *path_d;
	char *t_path = copy_in_path(p, path, path_l);
	if (t_path == NULL)
		return -1;
	/* TODO: 9ns support */
	path_d = lookup_dentry(t_path, 0);
	if (!path_d){
		int n = 2048;
		buf = kmalloc(n*2, MEM_WAIT);
		struct dir *d = (void *)&buf[n];
 		/* try 9ns. */
		if (sysstat(t_path, buf, n) > 0) {
			printk("sysstat t_path %s\n", t_path);
			convM2D(buf, n, d, (char *)&d[1]);
			/* will be NULL if things did not work out */
			symname = d->muid;
		}
	} else
		symname = path_d->d_inode->i_op->readlink(path_d);

	free_path(p, t_path);

	if (symname){
		copy_amt = strnlen(symname, buf_l - 1) + 1;
		if (!memcpy_to_user_errno(p, u_buf, symname, copy_amt))
			ret = copy_amt - 1;
	}
	if (path_d)
		kref_put(&path_d->d_kref);
	if (buf)
		kfree(buf);
	printd("READLINK returning %s\n", u_buf);
	return ret;
}

static intreg_t sys_chdir(struct proc *p, pid_t pid, const char *path,
                          size_t path_l)
{
	int retval;
	char *t_path;
	struct proc *target = get_controllable_proc(p, pid);
	if (!target)
		return -1;
	t_path = copy_in_path(p, path, path_l);
	if (!t_path) {
		proc_decref(target);
		return -1;
	}
	/* TODO: 9ns support */
	retval = do_chdir(&target->fs_env, t_path);
	free_path(p, t_path);
	proc_decref(target);
	return retval;
}

static intreg_t sys_fchdir(struct proc *p, pid_t pid, int fd)
{
	struct file *file;
	int retval;
	struct proc *target = get_controllable_proc(p, pid);
	if (!target)
		return -1;
	file = get_file_from_fd(&p->open_files, fd);
	if (!file) {
		/* TODO: 9ns */
		set_errno(EBADF);
		proc_decref(target);
		return -1;
	}
	retval = do_fchdir(&target->fs_env, file);
	kref_put(&file->f_kref);
	proc_decref(target);
	return retval;
}

/* Note cwd_l is not a strlen, it's an absolute size */
intreg_t sys_getcwd(struct proc *p, char *u_cwd, size_t cwd_l)
{
	int retval = 0;
	char *kfree_this;
	char *k_cwd;
	k_cwd = do_getcwd(&p->fs_env, &kfree_this, cwd_l);
	if (!k_cwd)
		return -1;		/* errno set by do_getcwd */
	if (strlen(k_cwd) + 1 > cwd_l) {
		set_error(ERANGE, "getcwd buf too small, needed %d", strlen(k_cwd) + 1);
		retval = -1;
		goto out;
	}
	if (memcpy_to_user_errno(p, u_cwd, k_cwd, strlen(k_cwd) + 1))
		retval = -1;
out:
	kfree(kfree_this);
	return retval;
}

intreg_t sys_mkdir(struct proc *p, const char *path, size_t path_l, int mode)
{
	int retval;
	char *t_path = copy_in_path(p, path, path_l);
	if (!t_path)
		return -1;
	mode &= S_PMASK;
	mode &= ~p->fs_env.umask;
	retval = do_mkdir(t_path, mode);
	if (retval && (get_errno() == ENOENT)) {
		unset_errno();
		/* mixing plan9 and glibc here, make sure DMDIR doesn't overlap with any
		 * permissions */
		static_assert(!(S_PMASK & DMDIR));
		retval = syscreate(t_path, O_RDWR, DMDIR | mode);
	}
	free_path(p, t_path);
	return retval;
}

intreg_t sys_rmdir(struct proc *p, const char *path, size_t path_l)
{
	int retval;
	char *t_path = copy_in_path(p, path, path_l);
	if (!t_path)
		return -1;
	/* TODO: 9ns support */
	retval = do_rmdir(t_path);
	free_path(p, t_path);
	return retval;
}

intreg_t sys_tcgetattr(struct proc *p, int fd, void *termios_p)
{
	int retval = 0;
	/* TODO: actually support this call on tty FDs.  Right now, we just fake
	 * what my linux box reports for a bash pty. */
	struct termios *kbuf = kmalloc(sizeof(struct termios), 0);
	kbuf->c_iflag = 0x2d02;
	kbuf->c_oflag = 0x0005;
	kbuf->c_cflag = 0x04bf;
	kbuf->c_lflag = 0x8a3b;
	kbuf->c_line = 0x0;
	kbuf->c_ispeed = 0xf;
	kbuf->c_ospeed = 0xf;
	kbuf->c_cc[0] = 0x03;
	kbuf->c_cc[1] = 0x1c;
	kbuf->c_cc[2] = 0x7f;
	kbuf->c_cc[3] = 0x15;
	kbuf->c_cc[4] = 0x04;
	kbuf->c_cc[5] = 0x00;
	kbuf->c_cc[6] = 0x01;
	kbuf->c_cc[7] = 0xff;
	kbuf->c_cc[8] = 0x11;
	kbuf->c_cc[9] = 0x13;
	kbuf->c_cc[10] = 0x1a;
	kbuf->c_cc[11] = 0xff;
	kbuf->c_cc[12] = 0x12;
	kbuf->c_cc[13] = 0x0f;
	kbuf->c_cc[14] = 0x17;
	kbuf->c_cc[15] = 0x16;
	kbuf->c_cc[16] = 0xff;
	kbuf->c_cc[17] = 0x00;
	kbuf->c_cc[18] = 0x00;
	kbuf->c_cc[19] = 0x00;
	kbuf->c_cc[20] = 0x00;
	kbuf->c_cc[21] = 0x00;
	kbuf->c_cc[22] = 0x00;
	kbuf->c_cc[23] = 0x00;
	kbuf->c_cc[24] = 0x00;
	kbuf->c_cc[25] = 0x00;
	kbuf->c_cc[26] = 0x00;
	kbuf->c_cc[27] = 0x00;
	kbuf->c_cc[28] = 0x00;
	kbuf->c_cc[29] = 0x00;
	kbuf->c_cc[30] = 0x00;
	kbuf->c_cc[31] = 0x00;

	if (memcpy_to_user_errno(p, termios_p, kbuf, sizeof(struct termios)))
		retval = -1;
	kfree(kbuf);
	return retval;
}

intreg_t sys_tcsetattr(struct proc *p, int fd, int optional_actions,
                       const void *termios_p)
{
	/* TODO: do this properly too.  For now, we just say 'it worked' */
	return 0;
}

/* TODO: we don't have any notion of UIDs or GIDs yet, but don't let that stop a
 * process from thinking it can do these.  The other alternative is to have
 * glibc return 0 right away, though someone might want to do something with
 * these calls.  Someday. */
intreg_t sys_setuid(struct proc *p, uid_t uid)
{
	return 0;
}

intreg_t sys_setgid(struct proc *p, gid_t gid)
{
	return 0;
}

/* long bind(char* src_path, char* onto_path, int flag);
 *
 * The naming for the args in bind is messy historically.  We do:
 * 		bind src_path onto_path
 * plan9 says bind NEW OLD, where new is *src*, and old is *onto*.
 * Linux says mount --bind OLD NEW, where OLD is *src* and NEW is *onto*. */
intreg_t sys_nbind(struct proc *p,
                   char *src_path, size_t src_l,
                   char *onto_path, size_t onto_l,
                   unsigned int flag)

{
	int ret;
	char *t_srcpath = copy_in_path(p, src_path, src_l);
	if (t_srcpath == NULL) {
		printd("srcpath dup failed ptr %p size %d\n", src_path, src_l);
		return -1;
	}
	char *t_ontopath = copy_in_path(p, onto_path, onto_l);
	if (t_ontopath == NULL) {
		free_path(p, t_srcpath);
		printd("ontopath dup failed ptr %p size %d\n", onto_path, onto_l);
		return -1;
	}
	printd("sys_nbind: %s -> %s flag %d\n", t_srcpath, t_ontopath, flag);
	ret = sysbind(t_srcpath, t_ontopath, flag);
	free_path(p, t_srcpath);
	free_path(p, t_ontopath);
	return ret;
}

/* int mount(int fd, int afd, char* onto_path, int flag, char* aname); */
intreg_t sys_nmount(struct proc *p,
                    int fd,
                    char *onto_path, size_t onto_l,
                    unsigned int flag
			/* we ignore these */
			/* no easy way to pass this many args anyway. *
		    int afd,
                    char *auth, size_t auth_l*/)
{
	int ret;
	int afd;

	afd = -1;
	char *t_ontopath = copy_in_path(p, onto_path, onto_l);
	if (t_ontopath == NULL)
		return -1;
	ret = sysmount(fd, afd, t_ontopath, flag, /* spec or auth */"/");
	free_path(p, t_ontopath);
	return ret;
}

/* Unmount undoes the operation of a bind or mount.  Check out
 * http://plan9.bell-labs.com/magic/man2html/1/bind .  Though our mount takes an
 * FD, not servename (aka src_path), so it's not quite the same.
 *
 * To translate between Plan 9 and Akaros, old -> onto_path.  new -> src_path.
 *
 * For unmount, src_path / new is optional.  If set, we only unmount the
 * bindmount that came from src_path. */
intreg_t sys_nunmount(struct proc *p, char *src_path, int src_l,
                      char *onto_path, int onto_l)
{
	int ret;
	char *t_ontopath, *t_srcpath;
	t_ontopath = copy_in_path(p, onto_path, onto_l);
	if (t_ontopath == NULL)
		return -1;
	if (src_path) {
		t_srcpath = copy_in_path(p, src_path, src_l);
		if (t_srcpath == NULL) {
			free_path(p, t_ontopath);
			return -1;
		}
	} else {
		t_srcpath = 0;
	}
	ret = sysunmount(t_srcpath, t_ontopath);
	free_path(p, t_ontopath);
	free_path(p, t_srcpath);	/* you can free a null path */
	return ret;
}

intreg_t sys_fd2path(struct proc *p, int fd, void *u_buf, size_t len)
{
	int ret = 0;
	struct chan *ch;
	ERRSTACK(1);
	/* UMEM: Check the range, can PF later and kill if the page isn't present */
	if (!is_user_rwaddr(u_buf, len)) {
		printk("[kernel] bad user addr %p (+%p) in %s (user bug)\n", u_buf,
		       len, __FUNCTION__);
		return -1;
	}
	/* fdtochan throws */
	if (waserror()) {
		poperror();
		return -1;
	}
	ch = fdtochan(&current->open_files, fd, -1, FALSE, TRUE);
	if (snprintf(u_buf, len, "%s", channame(ch)) >= len) {
		set_error(ERANGE, "fd2path buf too small, needed %d", ret);
		ret = -1;
	}
	cclose(ch);
	poperror();
	return ret;
}

/* Helper, interprets the wstat and performs the VFS action.  Returns stat_sz on
 * success for all ops, -1 or 0 o/w.  If one op fails, it'll skip the remaining
 * ones. */
static int vfs_wstat(struct file *file, uint8_t *stat_m, size_t stat_sz,
                     int flags)
{
	struct dir *dir;
	int m_sz;
	int retval = 0;

	dir = kzmalloc(sizeof(struct dir) + stat_sz, MEM_WAIT);
	m_sz = convM2D(stat_m, stat_sz, &dir[0], (char*)&dir[1]);
	if (m_sz != stat_sz) {
		set_error(EINVAL, ERROR_FIXME);
		kfree(dir);
		return -1;
	}
	if (flags & WSTAT_MODE) {
		retval = do_file_chmod(file, dir->mode);
		if (retval < 0)
			goto out;
	}
	if (flags & WSTAT_LENGTH) {
		retval = do_truncate(file->f_dentry->d_inode, dir->length);
		if (retval < 0)
			goto out;
	}
	if (flags & WSTAT_ATIME) {
		/* wstat only gives us seconds */
		file->f_dentry->d_inode->i_atime.tv_sec = dir->atime;
		file->f_dentry->d_inode->i_atime.tv_nsec = 0;
	}
	if (flags & WSTAT_MTIME) {
		file->f_dentry->d_inode->i_mtime.tv_sec = dir->mtime;
		file->f_dentry->d_inode->i_mtime.tv_nsec = 0;
	}

out:
	kfree(dir);
	/* convert vfs retval to wstat retval */
	if (retval >= 0)
		retval = stat_sz;
	return retval;
}

intreg_t sys_wstat(struct proc *p, char *path, size_t path_l,
                   uint8_t *stat_m, size_t stat_sz, int flags)
{
	int retval = 0;
	char *t_path = copy_in_path(p, path, path_l);
	struct file *file;

	if (!t_path)
		return -1;
	retval = syswstat(t_path, stat_m, stat_sz);
	if (retval == stat_sz) {
		free_path(p, t_path);
		return stat_sz;
	}
	/* 9ns failed, we'll need to check the VFS */
	file = do_file_open(t_path, O_READ, 0);
	free_path(p, t_path);
	if (!file)
		return -1;
	retval = vfs_wstat(file, stat_m, stat_sz, flags);
	kref_put(&file->f_kref);
	return retval;
}

intreg_t sys_fwstat(struct proc *p, int fd, uint8_t *stat_m, size_t stat_sz,
                    int flags)
{
	int retval = 0;
	struct file *file;

	retval = sysfwstat(fd, stat_m, stat_sz);
	if (retval == stat_sz)
		return stat_sz;
	/* 9ns failed, we'll need to check the VFS */
	file = get_file_from_fd(&p->open_files, fd);
	if (!file)
		return -1;
	retval = vfs_wstat(file, stat_m, stat_sz, flags);
	kref_put(&file->f_kref);
	return retval;
}

intreg_t sys_rename(struct proc *p, char *old_path, size_t old_path_l,
                    char *new_path, size_t new_path_l)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	ERRSTACK(1);
	int mountpointlen = 0;
	char *from_path = copy_in_path(p, old_path, old_path_l);
	char *to_path = copy_in_path(p, new_path, new_path_l);
	struct chan *oldchan = 0, *newchan = NULL;
	int retval = -1;

	if ((!from_path) || (!to_path))
		return -1;
	printd("sys_rename :%s: to :%s: : ", from_path, to_path);

	/* we need a fid for the wstat. */
	/* TODO: maybe wrap the 9ns stuff better.  sysrename maybe? */

	/* discard namec error */
	if (!waserror()) {
		oldchan = namec(from_path, Aaccess, 0, 0);
	}
	poperror();
	if (!oldchan) {
		retval = do_rename(from_path, to_path);
		free_path(p, from_path);
		free_path(p, to_path);
		return retval;
	}

	printd("Oldchan: %C\n", oldchan);
	printd("Oldchan: mchan %C\n", oldchan->mchan);

	/* If walked through a mountpoint, we need to take that
	 * into account for the Twstat.
	 */
	if (oldchan->mountpoint) {
		printd("mountpoint: %C\n", oldchan->mountpoint);
		if (oldchan->mountpoint->name)
			mountpointlen = oldchan->mountpoint->name->len;
	}

	/* This test makes sense even when mountpointlen is 0 */
	if (strlen(to_path) < mountpointlen) {
		set_errno(EINVAL);
		goto done;
	}

	/* the omode and perm are of no importance. */
	newchan = namec(to_path, Acreatechan, 0, 0);
	if (newchan == NULL) {
		printd("sys_rename %s to %s found no chan\n", from_path, to_path);
		set_errno(EPERM);
		goto done;
	}
	printd("Newchan: %C\n", newchan);
	printd("Newchan: mchan %C\n", newchan->mchan);

	if ((newchan->dev != oldchan->dev) ||
		(newchan->type != oldchan->type)) {
		printd("Old chan and new chan do not match\n");
		set_errno(ENODEV);
		goto done;
	}

	struct dir dir;
	size_t mlen;
	uint8_t mbuf[STATFIXLEN + MAX_PATH_LEN + 1];

	init_empty_dir(&dir);
	dir.name = to_path;
	/* absolute paths need the mountpoint name stripped from them.
	 * Once stripped, it still has to be an absolute path.
	 */
	if (dir.name[0] == '/') {
		dir.name = to_path + mountpointlen;
		if (dir.name[0] != '/') {
			set_errno(EINVAL);
			goto done;
		}
	}

	mlen = convD2M(&dir, mbuf, sizeof(mbuf));
	if (!mlen) {
		printk("convD2M failed\n");
		set_errno(EINVAL);
		goto done;
	}

	if (waserror()) {
		printk("validstat failed: %s\n", current_errstr());
		goto done;
	}

	validstat(mbuf, mlen, 1);
	poperror();

	if (waserror()) {
		//cclose(oldchan);
		nexterror();
	}

	retval = devtab[oldchan->type].wstat(oldchan, mbuf, mlen);

	poperror();
	if (retval == mlen) {
		retval = mlen;
	} else {
		printk("syswstat did not go well\n");
		set_errno(EXDEV);
	};
	printk("syswstat returns %d\n", retval);

done:
	free_path(p, from_path);
	free_path(p, to_path);
	cclose(oldchan);
	cclose(newchan);
	return retval;
}

/* Careful: if an FD is busy, we don't close the old object, it just fails */
static intreg_t sys_dup_fds_to(struct proc *p, unsigned int pid,
                               struct childfdmap *map, unsigned int nentries)
{
	ssize_t ret = 0;
	struct proc *child;
	int slot;
	struct file *file;

	if (!is_user_rwaddr(map, sizeof(struct childfdmap) * nentries)) {
		set_errno(EINVAL);
		return -1;
	}
	child = get_controllable_proc(p, pid);
	if (!child)
		return -1;
	for (int i = 0; i < nentries; i++) {
		map[i].ok = -1;
		file = get_file_from_fd(&p->open_files, map[i].parentfd);
		if (file) {
			slot = insert_file(&child->open_files, file, map[i].childfd, TRUE,
			                   FALSE);
			if (slot == map[i].childfd) {
				map[i].ok = 0;
				ret++;
			}
			kref_put(&file->f_kref);
			continue;
		}
		if (!sys_dup_to(p, map[i].parentfd, child, map[i].childfd)) {
			map[i].ok = 0;
			ret++;
			continue;
		}
		/* probably a bug, could send EBADF, maybe via 'ok' */
		printk("[kernel] dup_fds_to: couldn't find %d\n", map[i].parentfd);
	}
	proc_decref(child);
	return ret;
}

/* 0 on success, anything else is an error, with errno/errstr set */
static int handle_tap_req(struct proc *p, struct fd_tap_req *req)
{
	switch (req->cmd) {
		case (FDTAP_CMD_ADD):
			return add_fd_tap(p, req);
		case (FDTAP_CMD_REM):
			return remove_fd_tap(p, req->fd);
		default:
			set_error(ENOSYS, "FD Tap Command %d not supported", req->cmd);
			return -1;
	}
}

/* Processes up to nr_reqs tap requests.  If a request errors out, we stop
 * immediately.  Returns the number processed.  If done != nr_reqs, check errno
 * and errstr for the last failure, which is for tap_reqs[done]. */
static intreg_t sys_tap_fds(struct proc *p, struct fd_tap_req *tap_reqs,
                            size_t nr_reqs)
{
	struct fd_tap_req *req_i = tap_reqs;
	int done;
	if (!is_user_rwaddr(tap_reqs, sizeof(struct fd_tap_req) * nr_reqs)) {
		set_errno(EINVAL);
		return 0;
	}
	for (done = 0; done < nr_reqs; done++, req_i++) {
		if (handle_tap_req(p, req_i))
			break;
	}
	return done;
}

/************** Syscall Invokation **************/

const struct sys_table_entry syscall_table[] = {
	[SYS_null] = {(syscall_t)sys_null, "null"},
	[SYS_block] = {(syscall_t)sys_block, "block"},
	[SYS_cache_invalidate] = {(syscall_t)sys_cache_invalidate, "wbinv"},
	[SYS_reboot] = {(syscall_t)reboot, "reboot!"},
	[SYS_getpcoreid] = {(syscall_t)sys_getpcoreid, "getpcoreid"},
	[SYS_getvcoreid] = {(syscall_t)sys_getvcoreid, "getvcoreid"},
	[SYS_proc_create] = {(syscall_t)sys_proc_create, "proc_create"},
	[SYS_proc_run] = {(syscall_t)sys_proc_run, "proc_run"},
	[SYS_proc_destroy] = {(syscall_t)sys_proc_destroy, "proc_destroy"},
	[SYS_proc_yield] = {(syscall_t)sys_proc_yield, "proc_yield"},
	[SYS_change_vcore] = {(syscall_t)sys_change_vcore, "change_vcore"},
	[SYS_fork] = {(syscall_t)sys_fork, "fork"},
	[SYS_exec] = {(syscall_t)sys_exec, "exec"},
	[SYS_waitpid] = {(syscall_t)sys_waitpid, "waitpid"},
	[SYS_mmap] = {(syscall_t)sys_mmap, "mmap"},
	[SYS_munmap] = {(syscall_t)sys_munmap, "munmap"},
	[SYS_mprotect] = {(syscall_t)sys_mprotect, "mprotect"},
	[SYS_shared_page_alloc] = {(syscall_t)sys_shared_page_alloc, "pa"},
	[SYS_shared_page_free] = {(syscall_t)sys_shared_page_free, "pf"},
	[SYS_provision] = {(syscall_t)sys_provision, "provision"},
	[SYS_notify] = {(syscall_t)sys_notify, "notify"},
	[SYS_self_notify] = {(syscall_t)sys_self_notify, "self_notify"},
	[SYS_send_event] = {(syscall_t)sys_send_event, "send_event"},
	[SYS_vc_entry] = {(syscall_t)sys_vc_entry, "vc_entry"},
	[SYS_halt_core] = {(syscall_t)sys_halt_core, "halt_core"},
#ifdef CONFIG_ARSC_SERVER
	[SYS_init_arsc] = {(syscall_t)sys_init_arsc, "init_arsc"},
#endif
	[SYS_change_to_m] = {(syscall_t)sys_change_to_m, "change_to_m"},
	[SYS_vmm_setup] = {(syscall_t)sys_vmm_setup, "vmm_setup"},
	[SYS_vmm_poke_guest] = {(syscall_t)sys_vmm_poke_guest, "vmm_poke_guest"},
	[SYS_poke_ksched] = {(syscall_t)sys_poke_ksched, "poke_ksched"},
	[SYS_abort_sysc] = {(syscall_t)sys_abort_sysc, "abort_sysc"},
	[SYS_abort_sysc_fd] = {(syscall_t)sys_abort_sysc_fd, "abort_sysc_fd"},
	[SYS_populate_va] = {(syscall_t)sys_populate_va, "populate_va"},
	[SYS_nanosleep] = {(syscall_t)sys_nanosleep, "nanosleep"},
	[SYS_pop_ctx] = {(syscall_t)sys_pop_ctx, "pop_ctx"},

	[SYS_read] = {(syscall_t)sys_read, "read"},
	[SYS_write] = {(syscall_t)sys_write, "write"},
	[SYS_openat] = {(syscall_t)sys_openat, "openat"},
	[SYS_close] = {(syscall_t)sys_close, "close"},
	[SYS_fstat] = {(syscall_t)sys_fstat, "fstat"},
	[SYS_stat] = {(syscall_t)sys_stat, "stat"},
	[SYS_lstat] = {(syscall_t)sys_lstat, "lstat"},
	[SYS_fcntl] = {(syscall_t)sys_fcntl, "fcntl"},
	[SYS_access] = {(syscall_t)sys_access, "access"},
	[SYS_umask] = {(syscall_t)sys_umask, "umask"},
	[SYS_llseek] = {(syscall_t)sys_llseek, "llseek"},
	[SYS_link] = {(syscall_t)sys_link, "link"},
	[SYS_unlink] = {(syscall_t)sys_unlink, "unlink"},
	[SYS_symlink] = {(syscall_t)sys_symlink, "symlink"},
	[SYS_readlink] = {(syscall_t)sys_readlink, "readlink"},
	[SYS_chdir] = {(syscall_t)sys_chdir, "chdir"},
	[SYS_fchdir] = {(syscall_t)sys_fchdir, "fchdir"},
	[SYS_getcwd] = {(syscall_t)sys_getcwd, "getcwd"},
	[SYS_mkdir] = {(syscall_t)sys_mkdir, "mkdir"},
	[SYS_rmdir] = {(syscall_t)sys_rmdir, "rmdir"},
	[SYS_tcgetattr] = {(syscall_t)sys_tcgetattr, "tcgetattr"},
	[SYS_tcsetattr] = {(syscall_t)sys_tcsetattr, "tcsetattr"},
	[SYS_setuid] = {(syscall_t)sys_setuid, "setuid"},
	[SYS_setgid] = {(syscall_t)sys_setgid, "setgid"},
	/* special! */
	[SYS_nbind] ={(syscall_t)sys_nbind, "nbind"},
	[SYS_nmount] ={(syscall_t)sys_nmount, "nmount"},
	[SYS_nunmount] ={(syscall_t)sys_nunmount, "nunmount"},
	[SYS_fd2path] ={(syscall_t)sys_fd2path, "fd2path"},
	[SYS_wstat] ={(syscall_t)sys_wstat, "wstat"},
	[SYS_fwstat] ={(syscall_t)sys_fwstat, "fwstat"},
	[SYS_rename] ={(syscall_t)sys_rename, "rename"},
	[SYS_dup_fds_to] = {(syscall_t)sys_dup_fds_to, "dup_fds_to"},
	[SYS_tap_fds] = {(syscall_t)sys_tap_fds, "tap_fds"},
};
const int max_syscall = sizeof(syscall_table)/sizeof(syscall_table[0]);

/* Executes the given syscall.
 *
 * Note tf is passed in, which points to the tf of the context on the kernel
 * stack.  If any syscall needs to block, it needs to save this info, as well as
 * any silly state.
 *
 * This syscall function is used by both local syscall and arsc, and should
 * remain oblivious of the caller. */
intreg_t syscall(struct proc *p, uintreg_t sc_num, uintreg_t a0, uintreg_t a1,
                 uintreg_t a2, uintreg_t a3, uintreg_t a4, uintreg_t a5)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	intreg_t ret = -1;
	ERRSTACK(1);

	if (sc_num > max_syscall || syscall_table[sc_num].call == NULL) {
		printk("[kernel] Invalid syscall %d for proc %d\n", sc_num, p->pid);
		printk("\tArgs: %p, %p, %p, %p, %p, %p\n", a0, a1, a2, a3, a4, a5);
		print_user_ctx(per_cpu_info[core_id()].cur_ctx);
		return -1;
	}

	/* N.B. This is going away. */
	if (waserror()){
		printk("Plan 9 system call returned via waserror()\n");
		printk("String: '%s'\n", current_errstr());
		/* if we got here, then the errbuf was right.
		 * no need to check!
		 */
		return -1;
	}
	//printd("before syscall errstack %p\n", errstack);
	//printd("before syscall errstack base %p\n", get_cur_errbuf());
	ret = syscall_table[sc_num].call(p, a0, a1, a2, a3, a4, a5);
	//printd("after syscall errstack base %p\n", get_cur_errbuf());
	if (get_cur_errbuf() != &errstack[0]) {
		/* Can't trust coreid and vcoreid anymore, need to check the trace */
		printk("[%16llu] Syscall %3d (%12s):(%p, %p, %p, %p, "
		       "%p, %p) proc: %d\n", read_tsc(),
		       sc_num, syscall_table[sc_num].name, a0, a1, a2, a3,
		       a4, a5, p->pid);
		if (sc_num != SYS_fork)
			printk("YOU SHOULD PANIC: errstack mismatch");
	}
	return ret;
}

/* Execute the syscall on the local core */
void run_local_syscall(struct syscall *sysc)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct proc *p = pcpui->cur_proc;

	/* In lieu of pinning, we just check the sysc and will PF on the user addr
	 * later (if the addr was unmapped).  Which is the plan for all UMEM. */
	if (!is_user_rwaddr(sysc, sizeof(struct syscall))) {
		printk("[kernel] bad user addr %p (+%p) in %s (user bug)\n", sysc,
		       sizeof(struct syscall), __FUNCTION__);
		return;
	}
	pcpui->cur_kthread->sysc = sysc;	/* let the core know which sysc it is */
	systrace_start_trace(pcpui->cur_kthread, sysc);
	pcpui = &per_cpu_info[core_id()];	/* reload again */
	alloc_sysc_str(pcpui->cur_kthread);
	/* syscall() does not return for exec and yield, so put any cleanup in there
	 * too. */
	sysc->retval = syscall(pcpui->cur_proc, sysc->num, sysc->arg0, sysc->arg1,
	                       sysc->arg2, sysc->arg3, sysc->arg4, sysc->arg5);
	/* Need to re-load pcpui, in case we migrated */
	pcpui = &per_cpu_info[core_id()];
	free_sysc_str(pcpui->cur_kthread);
	systrace_finish_trace(pcpui->cur_kthread, sysc->retval);
	pcpui = &per_cpu_info[core_id()];	/* reload again */
	/* Some 9ns paths set errstr, but not errno.  glibc will ignore errstr.
	 * this is somewhat hacky, since errno might get set unnecessarily */
	if ((current_errstr()[0] != 0) && (!sysc->err))
		sysc->err = EUNSPECIFIED;
	finish_sysc(sysc, pcpui->cur_proc);
	pcpui->cur_kthread->sysc = NULL;	/* No longer working on sysc */
}

/* A process can trap and call this function, which will set up the core to
 * handle all the syscalls.  a.k.a. "sys_debutante(needs, wants)".  If there is
 * at least one, it will run it directly. */
void prep_syscalls(struct proc *p, struct syscall *sysc, unsigned int nr_syscs)
{
	/* Careful with pcpui here, we could have migrated */
	if (!nr_syscs) {
		printk("[kernel] No nr_sysc, probably a bug, user!\n");
		return;
	}
	/* For all after the first call, send ourselves a KMSG (TODO). */
	if (nr_syscs != 1)
		warn("Only one supported (Debutante calls: %d)\n", nr_syscs);
	/* Call the first one directly.  (we already checked to make sure there is
	 * 1) */
	run_local_syscall(sysc);
}

/* Call this when something happens on the syscall where userspace might want to
 * get signaled.  Passing p, since the caller should know who the syscall
 * belongs to (probably is current).
 *
 * You need to have SC_K_LOCK set when you call this. */
void __signal_syscall(struct syscall *sysc, struct proc *p)
{
	struct event_queue *ev_q;
	struct event_msg local_msg;
	/* User sets the ev_q then atomically sets the flag (races with SC_DONE) */
	if (atomic_read(&sysc->flags) & SC_UEVENT) {
		rmb();	/* read the ev_q after reading the flag */
		ev_q = sysc->ev_q;
		if (ev_q) {
			memset(&local_msg, 0, sizeof(struct event_msg));
			local_msg.ev_type = EV_SYSCALL;
			local_msg.ev_arg3 = sysc;
			send_event(p, ev_q, &local_msg, 0);
		}
	}
}

bool syscall_uses_fd(struct syscall *sysc, int fd)
{
	switch (sysc->num) {
		case (SYS_read):
		case (SYS_write):
		case (SYS_close):
		case (SYS_fstat):
		case (SYS_fcntl):
		case (SYS_llseek):
		case (SYS_nmount):
		case (SYS_fd2path):
			if (sysc->arg0 == fd)
				return TRUE;
			return FALSE;
		case (SYS_mmap):
			/* mmap always has to be special. =) */
			if (sysc->arg4 == fd)
				return TRUE;
			return FALSE;
		default:
			return FALSE;
	}
}

void print_sysc(struct proc *p, struct syscall *sysc)
{
	uintptr_t old_p = switch_to(p);
	printk("SYS_%d, flags %p, a0 %p, a1 %p, a2 %p, a3 %p, a4 %p, a5 %p\n",
	       sysc->num, atomic_read(&sysc->flags),
	       sysc->arg0, sysc->arg1, sysc->arg2, sysc->arg3, sysc->arg4,
	       sysc->arg5);
	switch_back(p, old_p);
}
