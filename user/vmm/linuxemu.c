/* Copyright (c) 2016 Google Inc.
 * See LICENSE for details.
 *
 * Linux emulation for virtual machines. */

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <vmm/vmm.h>
#include <errno.h>
#include <sys/syscall.h>
#include <vmm/linux_syscalls.h>
#include <sys/time.h>
#include <vmm/linuxemu.h>
#include <dlfcn.h>


static int lemu_debug;
static uth_mutex_t *lemu_logging_lock;
static FILE *lemu_global_logfile;

void init_lemu_logging(int log_level)
{
	const char *logfile_name = "lemu.log";
	FILE *x = fopen(logfile_name, "w");

	lemu_debug = log_level;
	lemu_logging_lock = uth_mutex_alloc();

	if (x != NULL)
		lemu_global_logfile = x;
	else
		lemu_global_logfile = stderr;
}

void destroy_lemu_logging(void)
{
	if (lemu_logging_lock != NULL)
		uth_mutex_free(lemu_logging_lock);

	if (lemu_global_logfile != stderr)
		fclose(lemu_global_logfile);
}


void lemuprint(const uint32_t tid, const char *syscallname,
               const bool isError, const char *fmt, ...)
{
	va_list valist;
	const char *prefix = "[TID %d] %s: ";
	bool double_logging = false;


	// Do not use global variable as a check to acquire lock.
	// make sure it is not changed during our acquire/release.
	int debug = lemu_debug;

	// If we are not going to log anything anyway, just bail out.
	if (!(debug > 0 || isError))
		return;

	va_start(valist, fmt);

	uth_mutex_lock(lemu_logging_lock);

	// Print to stderr if debug level is sufficient
	if (debug > 1) {
		fprintf(stderr, prefix, tid, syscallname);
		vfprintf(stderr, fmt, valist);
		// Checks if we will double log to stderr
		if (lemu_global_logfile == stderr)
			double_logging = true;
	}

	// Log to the global logfile, if we defaulted the global logging to
	// stderr then we don't want to log 2 times to stderr.
	if (lemu_global_logfile != NULL && !double_logging) {
		fprintf(lemu_global_logfile, prefix, tid, syscallname);
		vfprintf(lemu_global_logfile, fmt, valist);
	}

	uth_mutex_unlock(lemu_logging_lock);

	va_end(valist);
}


bool dune_sys_read(struct vm_trapframe *tf)
{
	ssize_t retval = read(tf->tf_rdi, (void*) tf->tf_rsi, (size_t) tf->tf_rdx);
	int err = errno;

	if (retval == -1) {
		lemuprint(tf->tf_guest_pcoreid, syscalls[tf->tf_rax], true,
		          "ERROR %zd\n", err);
		tf->tf_rax = -err;
	} else {
		lemuprint(tf->tf_guest_pcoreid, syscalls[tf->tf_rax], false,
		          "SUCCESS %zd\n", retval);
		tf->tf_rax = retval;
	}
	return true;
}


bool dune_sys_write(struct vm_trapframe *tf)
{
	ssize_t retval = write((int) tf->tf_rdi, (const void *) tf->tf_rsi,
	                       (size_t) tf->tf_rdx);
	int err = errno;

	if (retval == -1) {
		lemuprint(tf->tf_guest_pcoreid, syscalls[tf->tf_rax], true,
		          "ERROR %zd\n", err);
		tf->tf_rax = -err;
	} else {
		lemuprint(tf->tf_guest_pcoreid, syscalls[tf->tf_rax], false,
		          "SUCCESS %zd\n", retval);
		tf->tf_rax = retval;
	}
	return true;
}

bool dune_sys_gettid(struct vm_trapframe *tf)
{
	tf->tf_rax = tf->tf_guest_pcoreid;
	return true;
}

bool dune_sys_gettimeofday(struct vm_trapframe *tf)
{
	int retval = gettimeofday((struct timeval*) tf->tf_rdi,
	                          (struct timezone*) tf->tf_rsi);
	int err = errno;

	if (retval == -1) {
		lemuprint(tf->tf_guest_pcoreid, syscalls[tf->tf_rax], true,
		          "ERROR %d\n", err);
		tf->tf_rax = -err;
	} else {
		lemuprint(tf->tf_guest_pcoreid, syscalls[tf->tf_rax], false,
		          "SUCCESS %d\n", retval);
		tf->tf_rax = retval;
	}
	return true;
}

void init_syscall_table(void)
{
	int i;

	for (i = 0; i < dune_max_syscall ; ++i) {
		dune_syscall_table[i].call = NULL;
		dune_syscall_table[i].name = "nosyscall";
	}
	// For now setup the syscalls here,
	// there is probably a better way to do this.
	dune_syscall_table[DUNE_SYS_WRITE].call = dune_sys_write;
	dune_syscall_table[DUNE_SYS_WRITE].name = syscalls[DUNE_SYS_WRITE];
	dune_syscall_table[DUNE_SYS_GETTID].call = dune_sys_gettid;
	dune_syscall_table[DUNE_SYS_GETTID].name = syscalls[DUNE_SYS_GETTID];
	dune_syscall_table[DUNE_SYS_GETTIMEOFDAY].call = dune_sys_gettimeofday;
	dune_syscall_table[DUNE_SYS_GETTIMEOFDAY].name =
		syscalls[DUNE_SYS_GETTIMEOFDAY];
	dune_syscall_table[DUNE_SYS_READ].call = dune_sys_read;
	dune_syscall_table[DUNE_SYS_READ].name = syscalls[DUNE_SYS_READ];
	if (dlopen("liblinuxemu_extend.so", RTLD_NOW) == NULL)
		fprintf(stderr, "Not using any syscall extensions\n Reason: %s\n",
		        dlerror());

}


/* TODO: have an array which classifies syscall args
 * and "special" system calls (ones with weird return
 * values etc.). For some cases, we don't even do a system
 * call, and in many cases we have to rearrange arguments
 * since Linux and Akaros don't share signatures, so this
 * gets tricky. */
bool
linuxemu(struct guest_thread *gth, struct vm_trapframe *tf)
{
	bool ret = false;

	if (tf->tf_rax >= dune_max_syscall) {
		fprintf(stderr, "System call %d is out of range\n", tf->tf_rax);
		return false;
	}


	if (dune_syscall_table[tf->tf_rax].call == NULL) {
		fprintf(stderr, "System call #%d (%s) is not implemented\n",
		        tf->tf_rax, dune_syscall_table[tf->tf_rax].name);
		return false;
	}

	lemuprint(tf->tf_guest_pcoreid, dune_syscall_table[tf->tf_rax].name,
	          false, "vmcall(%d, %p, %p, %p, %p, %p, %p);\n", tf->tf_rax,
	          tf->tf_rdi, tf->tf_rsi, tf->tf_rdx, tf->tf_r10, tf->tf_r8,
	          tf->tf_r9);

	tf->tf_rip += 3;

	return (dune_syscall_table[tf->tf_rax].call)(tf);
}
