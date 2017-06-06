/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * ELF loading. */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <libelf.h>

/* load_elf loads and ELF file. This is almost always a kernel.
 * We assume that memory is set up correctly, and it will go hard
 * with you if it is not. */
uintptr_t
load_elf(char *filename)
{
	Elf64_Ehdr *ehdr;
	Elf *elf;
	size_t phnum = 0;
	Elf64_Phdr *hdrs;
	int fd;
	uintptr_t ret;

	elf_version(EV_CURRENT);
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s: %r\n", filename);
		return 0;
	}

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf == NULL) {
		fprintf(stderr, "%s: cannot read %s ELF file.\n", __func__, filename);
		close(fd);
		return 0;
	}

	ehdr = elf64_getehdr(elf);
	if (ehdr == NULL) {
		fprintf(stderr, "%s: cannot get exec header of %s.\n",
		        __func__, filename);
		goto fail;
	}
	fprintf(stderr, "%s ELF entry point is %p\n", filename,
	        (void *)ehdr->e_entry);

	if (elf_getphdrnum(elf, &phnum) < 0) {
		fprintf(stderr, "%s: cannot get program header num of %s.\n",
		        __func__, filename);
		goto fail;
	}
	fprintf(stderr, "%s has %p program headers\n", filename, phnum);

	hdrs = elf64_getphdr(elf);
	if (hdrs == NULL) {
		fprintf(stderr, "%s: cannot get program headers of %s.\n",
		        __func__, filename);
		goto fail;
	}

	for (int i = 0; i < phnum; i++) {
		size_t tot;
		Elf64_Phdr *h = &hdrs[i];
		uintptr_t pa;

		fprintf(stderr,
		        "%d: type 0x%lx flags 0x%lx  offset 0x%lx vaddr 0x%lx paddr 0x%lx size 0x%lx  memsz 0x%lx align 0x%lx\n",
		        i,
		        h->p_type,              /* Segment type */
		        h->p_flags,             /* Segment flags */
		        h->p_offset,            /* Segment file offset */
		        h->p_vaddr,             /* Segment virtual address */
		        h->p_paddr,             /* Segment physical address */
		        h->p_filesz,            /* Segment size in file */
		        h->p_memsz,             /* Segment size in memory */
		        h->p_align              /* Segment alignment */);
		if (h->p_type != PT_LOAD)
			continue;
		if ((h->p_flags & (PF_R | PF_W | PF_X)) == 0)
			continue;

		pa = h->p_paddr;
		fprintf(stderr,
		        "Read header %d @offset %p to %p (elf PA is %p) %d bytes:",
		        i, h->p_offset, pa, h->p_paddr, h->p_filesz);
		tot = 0;
		while (tot < h->p_filesz) {
			int amt = pread(fd, (void *)(pa + tot), h->p_filesz - tot,
			                h->p_offset + tot);

			if (amt < 1)
				break;
			tot += amt;
		}
		fprintf(stderr, "read a total of %d bytes\n", tot);
		if (tot < h->p_filesz) {
			fprintf(stderr, "%s: got %d bytes, wanted %d bytes\n",
			        filename, tot, h->p_filesz);
			goto fail;
		}
	}

	close(fd);
	ret = ehdr->e_entry;
	elf_end(elf);
	return ret;
fail:
	close(fd);
	elf_end(elf);
	return 0;
}
