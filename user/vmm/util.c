/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * Utility functions. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vmm/util.h>
#include <vmm/vmm.h>

ssize_t cat(char *filename, void *where, size_t memsize)
{
	int fd;
	ssize_t amt, tot = 0;
	struct stat buf;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s: %r\n", filename);
		return -1;
	}

	if (fstat(fd, &buf) < 0) {
		fprintf(stderr, "Can't stat %s: %r\n", filename);
		close(fd);
		return -1;
	}

	if (buf.st_size > memsize) {
		fprintf(stderr,
		        "file is %d bytes, but we only have %d bytes to place it\n",
		        buf.st_size, memsize);
		errno = ENOMEM;
		close(fd);
		return -1;
	}

	while (tot < buf.st_size) {
		amt = read(fd, where, buf.st_size - tot);
		if (amt < 0) {
			tot = -1;
			break;
		}
		if (amt == 0)
			break;
		where += amt;
		tot += amt;
	}

	close(fd);
	return tot;
}

void backtrace_guest_thread(FILE *f, struct guest_thread *vm_thread)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(vm_thread);
	uintptr_t guest_pc, guest_fp, host_fp;
	uintptr_t frame[2];

	guest_pc = vm_tf->tf_rip;
	guest_fp = vm_tf->tf_rbp;

	/* The BT should work for any code using frame pointers.  Most of the time,
	 * this will be vmlinux, and calling it that helps our backtrace.  This
	 * spits out the format that is parsed by bt-akaros.sh. */
	fprintf(f, "Backtracing guest, vmlinux is assumed, but check addrs\n");
	for (int i = 1; i < 30; i++) {
		fprintf(f, "#%02d Addr %p is in vmlinux at offset %p\n", i, guest_pc,
		        guest_pc);
		if (!guest_fp)
			break;
		if (gva2gpa(vm_thread, guest_fp, &host_fp))
			break;
		memcpy(frame, (void*)host_fp, 2 * sizeof(uintptr_t));
		guest_pc = frame[1];
		guest_fp = frame[0];
	}
}
