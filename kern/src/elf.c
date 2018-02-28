#include <mm.h>
#include <string.h>
#include <kmalloc.h>
#include <syscall.h>
#include <elf.h>
#include <pmap.h>
#include <smp.h>
#include <arch/arch.h>
#include <umem.h>

#ifdef CONFIG_64BIT
# define elf_field(obj, field) (elf64 ? (obj##64)->field : (obj##32)->field)
#else
# define elf_field(obj, field) ((obj##32)->field)
#endif

/* Check if the file is valid elf file (i.e. by checking for ELF_MAGIC in the
 * header) */
bool is_valid_elf(struct file_or_chan *foc)
{
	elf64_t h;
	uintptr_t c = switch_to_ktask();

	if (foc_read(foc, (char*)&h, sizeof(elf64_t), 0) != sizeof(elf64_t))
		goto fail;
	if (h.e_magic != ELF_MAGIC) {
		goto fail;
	}
success:
	switch_back_from_ktask(c);
	return TRUE;
fail:
	switch_back_from_ktask(c);
	return FALSE;
}

static uintptr_t populate_stack(struct proc *p, int argc, char *argv[],
                                                int envc, char *envp[],
                                                int auxc, elf_aux_t auxv[])
{
	/* Map in pages for p's stack. */
	int flags = MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE;
	uintptr_t stacksz = USTACK_NUM_PAGES*PGSIZE;
	if (do_mmap(p, USTACKTOP-stacksz, stacksz, PROT_READ | PROT_WRITE,
	            flags, NULL, 0) == MAP_FAILED)
		return 0;

	/* Function to get the lengths of the argument and environment strings. */
	int get_lens(int argc, char *argv[], int arg_lens[])
	{
		int total = 0;
		for (int i = 0; i < argc; i++) {
			arg_lens[i] = strlen(argv[i]) + 1;
			total += arg_lens[i];
		}
		return total;
	}

	/* Function to help map the argument and environment strings, to their
	 * final location. */
	int remap(int argc, char *argv[], char *new_argv[],
              char new_argbuf[], int arg_lens[])
	{
		int offset = 0;
		char *temp_argv[argc + 1];
		for(int i = 0; i < argc; i++) {
			if (memcpy_to_user(p, new_argbuf + offset, argv[i], arg_lens[i]))
				return -1;
			temp_argv[i] = new_argbuf + offset;
			offset += arg_lens[i];
		}
		temp_argv[argc] = NULL;
		if (memcpy_to_user(p, new_argv, temp_argv, sizeof(temp_argv)))
			return -1;
		return offset;
	}

	/* Start tracking the size of the buffer necessary to hold all of our data
	 * on the stack. Preallocate space for argc, argv, envp, and auxv in this
	 * buffer. */
	int bufsize = 0;
	bufsize += 1 * sizeof(size_t);
	bufsize += (auxc + 1) * sizeof(elf_aux_t);
	bufsize += (envc + 1) * sizeof(char**);
	bufsize += (argc + 1) * sizeof(char**);

	/* Add in the size of the env and arg strings. */
	int arg_lens[argc];
	int env_lens[envc];
	bufsize += get_lens(argc, argv, arg_lens);
	bufsize += get_lens(envc, envp, env_lens);

	/* Adjust bufsize so that our buffer will ultimately be 16 byte aligned. */
	bufsize = ROUNDUP(bufsize, 16);

	/* Set up pointers to all of the appropriate data regions we map to. */
	size_t *new_argc = (size_t*)(USTACKTOP - bufsize);
	char **new_argv = (char**)(new_argc + 1);
	char **new_envp = new_argv + argc + 1;
	elf_aux_t *new_auxv = (elf_aux_t*)(new_envp + envc + 1);
	char *new_argbuf = (char*)(new_auxv + auxc + 1);

	/* Verify that all data associated with our argv, envp, and auxv arrays
	 * (and any corresponding strings they point to) will fit in the space
	 * alloted. */
	if (bufsize > ARG_MAX)
		return 0;

	/* Map argc into its final location. */
	if (memcpy_to_user(p, new_argc, &argc, sizeof(size_t)))
		return 0;

	/* Map all data for argv and envp into its final location. */
	int offset = 0;
	offset = remap(argc, argv, new_argv, new_argbuf, arg_lens);
	if (offset == -1)
		return 0;
	offset = remap(envc, envp, new_envp, new_argbuf + offset, env_lens);
	if (offset == -1)
		return 0;

	/* Map auxv into its final location. */
	elf_aux_t null_aux = {0, 0};
	if (memcpy_to_user(p, new_auxv, auxv, auxc * sizeof(elf_aux_t)))
		return 0;
	if (memcpy_to_user(p, new_auxv + auxc, &null_aux, sizeof(elf_aux_t)))
		return 0;

	return USTACKTOP - bufsize;
}

/* We need the writable flag for ld.  Even though the elf header says it wants
 * RX (and not W) for its main program header, it will page fault (eip 56f0,
 * 46f0 after being relocated to 0x1000, va 0x20f4). */
static int load_one_elf(struct proc *p, struct file_or_chan *foc,
                        uintptr_t pg_num, elf_info_t *ei, bool writable)
{
	int ret = -1;
	ei->phdr = -1;
	ei->dynamic = 0;
	ei->highest_addr = 0;
	off64_t f_off = 0;
	void* phdrs = 0;
	int mm_perms, mm_flags;

	/* When reading on behalf of the kernel, we need to switch to a ktask so
	 * the VFS (and maybe other places) know. (TODO: KFOP) */
	uintptr_t old_ret = switch_to_ktask();

	/* Read in ELF header. */
	elf64_t elfhdr_storage;
	elf32_t* elfhdr32 = (elf32_t*)&elfhdr_storage;
	elf64_t* elfhdr64 = &elfhdr_storage;
	if (foc_read(foc, (char*)elfhdr64, sizeof(elf64_t), f_off)
	        != sizeof(elf64_t)) {
		/* if you ever debug this, be sure to 0 out elfhrd_storage in advance */
		printk("[kernel] load_one_elf: failed to read file\n");
		goto fail;
	}
	if (elfhdr64->e_magic != ELF_MAGIC) {
		printk("[kernel] load_one_elf: file is not an elf!\n");
		goto fail;
	}
	bool elf32 = elfhdr32->e_ident[ELF_IDENT_CLASS] == ELFCLASS32;
	bool elf64 = elfhdr64->e_ident[ELF_IDENT_CLASS] == ELFCLASS64;
	if (elf64 == elf32) {
		printk("[kernel] load_one_elf: ID as both 32 and 64 bit\n");
		goto fail;
	}
	#ifndef CONFIG_64BIT
	if (elf64) {
		printk("[kernel] load_one_elf: 64 bit elf on 32 bit kernel\n");
		goto fail;
	}
	#endif
	/* Not sure what RISCV's 64 bit kernel can do here, so this check is x86
	 * only */
	#ifdef CONFIG_X86
	if (elf32) {
		printk("[kernel] load_one_elf: 32 bit elf on 64 bit kernel\n");
		goto fail;
	}
	#endif

	size_t phsz = elf64 ? sizeof(proghdr64_t) : sizeof(proghdr32_t);
	uint16_t e_phnum = elf_field(elfhdr, e_phnum);
	uint16_t e_phoff = elf_field(elfhdr, e_phoff);

	/* Read in program headers. */
	if (e_phnum > 10000 || e_phoff % (elf32 ? 4 : 8) != 0) {
		printk("[kernel] load_one_elf: Bad program headers\n");
		goto fail;
	}
	phdrs = kmalloc(e_phnum * phsz, 0);
	f_off = e_phoff;
	if (!phdrs || foc_read(foc, phdrs, e_phnum * phsz, f_off) !=
	              e_phnum * phsz) {
		printk("[kernel] load_one_elf: could not get program headers\n");
		goto fail;
	}
	for (int i = 0; i < e_phnum; i++) {
		proghdr32_t* ph32 = (proghdr32_t*)phdrs + i;
		proghdr64_t* ph64 = (proghdr64_t*)phdrs + i;
		uint16_t p_type = elf_field(ph, p_type);
		uintptr_t p_va = elf_field(ph, p_va);
		uintptr_t p_offset = elf_field(ph, p_offset);
		uintptr_t p_align = elf_field(ph, p_align);
		uintptr_t p_memsz = elf_field(ph, p_memsz);
		uintptr_t p_filesz = elf_field(ph, p_filesz);
		uintptr_t p_flags = elf_field(ph, p_flags);

		/* Here's the ld hack, mentioned above */
		p_flags |= (writable ? ELF_PROT_WRITE : 0);
		/* All mmaps need to be fixed to their VAs.  If the program wants it to
		 * be a writable region, we also need the region to be private. */
		mm_flags = MAP_FIXED |
		           (p_flags & ELF_PROT_WRITE ? MAP_PRIVATE : MAP_SHARED);

		if (p_type == ELF_PROG_PHDR)
			ei->phdr = p_va;
		else if (p_type == ELF_PROG_INTERP) {
			f_off = p_offset;
			ssize_t maxlen = sizeof(ei->interp);
			ssize_t bytes = foc_read(foc, ei->interp, maxlen, f_off);
			/* trying to catch errors.  don't know how big it could be, but it
			 * should be at least 0. */
			if (bytes <= 0) {
				printk("[kernel] load_one_elf: could not read ei->interp\n");
				goto fail;
			}

			maxlen = MIN(maxlen, bytes);
			if (strnlen(ei->interp, maxlen) == maxlen) {
				printk("[kernel] load_one_elf: interpreter name too long\n");
				goto fail;
			}

			ei->dynamic = 1;
		}
		else if (p_type == ELF_PROG_LOAD && p_memsz) {
			if (p_align % PGSIZE) {
				printk("[kernel] load_one_elf: not page aligned\n");
				goto fail;
			}
			if (p_offset % PGSIZE != p_va % PGSIZE) {
				printk("[kernel] load_one_elf: offset difference \n");
				goto fail;
			}

			uintptr_t filestart = ROUNDDOWN(p_offset, PGSIZE);
			uintptr_t filesz = p_offset + p_filesz - filestart;

			uintptr_t memstart = ROUNDDOWN(p_va, PGSIZE);
			uintptr_t memsz = ROUNDUP(p_va + p_memsz, PGSIZE) - memstart;
			memstart += pg_num * PGSIZE;

			if (memstart + memsz > ei->highest_addr)
				ei->highest_addr = memstart + memsz;

			mm_perms = 0;
			mm_perms |= (p_flags & ELF_PROT_READ  ? PROT_READ : 0);
			mm_perms |= (p_flags & ELF_PROT_WRITE ? PROT_WRITE : 0);
			mm_perms |= (p_flags & ELF_PROT_EXEC  ? PROT_EXEC : 0);

			if (filesz) {
				/* Due to elf-ghetto-ness, we need to zero the first part of
				 * the BSS from the last page of the data segment.  If we end
				 * on a partial page, we map it in separately with
				 * MAP_POPULATE so that we can zero the rest of it now. We
				 * translate to the KVA so we don't need to worry about using
				 * the proc's mapping */
				uintptr_t partial = PGOFF(filesz);

				if (filesz - partial) {
					/* Map the complete pages. */
					if (do_mmap(p, memstart, filesz - partial, mm_perms,
					            mm_flags, foc, filestart) == MAP_FAILED) {
						printk("[kernel] load_one_elf: complete mmap failed\n");
						goto fail;
					}
				}
				/* Note that we (probably) only need to do this zeroing the end
				 * of a partial file page when we are dealing with
				 * ELF_PROT_WRITE-able PHs, and not for all cases.  */
				if (partial) {
					/* Need our own populated, private copy of the page so that
					 * we can zero the remainder - and not zero chunks of the
					 * real file in the page cache. */
					mm_flags &= ~MAP_SHARED;
					mm_flags |= MAP_PRIVATE | MAP_POPULATE;

					/* Map the final partial page. */
					uintptr_t last_page = memstart + filesz - partial;
					if (do_mmap(p, last_page, PGSIZE, mm_perms, mm_flags,
					            foc, filestart + filesz - partial)
						== MAP_FAILED) {
						printk("[kernel] load_one_elf: partial mmap failed\n");
						goto fail;
					}

					/* Zero the end of it.  This is a huge pain in the ass.  The
					 * filesystems should zero out the last bits of a page if
					 * the file doesn't fill the last page.  But we're dealing
					 * with windows into otherwise complete files. */
					pte_t pte = pgdir_walk(p->env_pgdir, (void*)last_page, 0);
					/* if we were able to get a PTE, then there is a real page
					 * backing the VMR, and we need to zero the excess.  if
					 * there isn't, then the page fault code should handle it.
					 * since we set populate above, we should have a PTE, except
					 * in cases where the offset + len window exceeded the file
					 * size.  in this case, we let them mmap it, but didn't
					 * populate it.  there will be a PF right away if someone
					 * tries to use this.  check out do_mmap for more info. */
					if (pte_walk_okay(pte)) {
						void* last_page_kva = KADDR(pte_get_paddr(pte));
						memset(last_page_kva + partial, 0, PGSIZE - partial);
					}

					filesz = ROUNDUP(filesz, PGSIZE);
				}
			}
			/* Any extra pages are mapped anonymously... (a bit weird) */
			if (filesz < memsz)
				if (do_mmap(p, memstart + filesz, memsz-filesz,
				            PROT_READ | PROT_WRITE, MAP_PRIVATE,
					        NULL, 0) == MAP_FAILED) {
					printk("[kernel] load_one_elf: anon mmap failed\n");
					goto fail;
				}
		}
	}
	/* map in program headers anyway if not present in binary.
	 * useful for TLS in static programs. */
	if (ei->phdr == -1) {
		uintptr_t filestart = ROUNDDOWN(e_phoff, PGSIZE);
		uintptr_t filesz = e_phoff + (e_phnum * phsz) - filestart;
		void *phdr_addr = do_mmap(p, 0, filesz, PROT_READ | PROT_WRITE,
		                          MAP_PRIVATE, foc, filestart);
		if (phdr_addr == MAP_FAILED) {
			printk("[kernel] load_one_elf: prog header mmap failed\n");
			goto fail;
		}
		ei->phdr = (long)phdr_addr + e_phoff;
	}
	ei->entry = elf_field(elfhdr, e_entry) + pg_num * PGSIZE;
	ei->phnum = e_phnum;
	ei->elf64 = elf64;
	ret = 0;
	/* Fall-through */
fail:
	if (phdrs)
		kfree(phdrs);
	switch_back_from_ktask(old_ret);
	return ret;
}

int load_elf(struct proc *p, struct file_or_chan *foc,
             int argc, char *argv[], int envc, char *envp[])
{
	elf_info_t ei, interp_ei;
	if (load_one_elf(p, foc, 0, &ei, FALSE))
		return -1;

	if (ei.dynamic) {
		struct file_or_chan *interp = foc_open(ei.interp, O_READ, 0);
		if (!interp)
			return -1;
		/* Load dynamic linker at 1M. Obvious MIB joke avoided.
		 * It used to be loaded at page 1, but the existence of valid addresses
		 * that low masked bad derefs through NULL pointer structs. This in turn
		 * helped us waste a full day debugging a bug in the Go runtime. True!
		 * Note that MMAP_LOWEST_VA also has this value but we want to make this
		 * explicit. */
		int error = load_one_elf(p, interp, MMAP_LD_FIXED_VA >> PGSHIFT,
		                         &interp_ei, TRUE);
		foc_decref(interp);
		if (error)
			return -1;
	}

	/* Set up the auxiliary info for dynamic linker/runtime */
	elf_aux_t auxv[] = {{ELF_AUX_PHDR, ei.phdr},
	                    {ELF_AUX_PHENT, sizeof(proghdr32_t)},
	                    {ELF_AUX_PHNUM, ei.phnum},
	                    {ELF_AUX_ENTRY, ei.entry}};
	int auxc = sizeof(auxv)/sizeof(auxv[0]);

	/* Populate the stack with the required info. */
	uintptr_t stack_top = populate_stack(p, argc, argv, envc, envp, auxc, auxv);
	if (!stack_top)
		return -1;

	/* Initialize the process as an SCP. */
	uintptr_t core0_entry = ei.dynamic ? interp_ei.entry : ei.entry;
	proc_init_ctx(&p->scp_ctx, 0, core0_entry, stack_top, 0);

	p->procinfo->program_end = ei.highest_addr;
	p->args_base = (void *) stack_top;

	return 0;
}

ssize_t get_startup_argc(struct proc *p)
{
	const char *sptr = (const char *) p->args_base;
	ssize_t argc = 0;

	/* TODO,DL: Use copy_from_user() when available.
	 */
	if (memcpy_from_user(p, &argc, sptr, sizeof(size_t)))
		return -1;

	return argc;
}

char *get_startup_argv(struct proc *p, size_t idx, char *argp,
					   size_t max_size)
{
	size_t stack_space = (const char *) USTACKTOP - (const char *) p->args_base;
	const char *sptr = (const char *) p->args_base + sizeof(size_t) +
		idx * sizeof(char *);
	const char *argv = NULL;

	/* TODO,DL: Use copy_from_user() when available.
	 */
	if (memcpy_from_user(p, &argv, sptr, sizeof(char *)))
		return NULL;

	/* TODO,DL: Use strncpy_from_user() when available.
	 */
	max_size = MIN(max_size, stack_space);
	if (memcpy_from_user(p, argp, argv, max_size))
		return NULL;
	argp[max_size - 1] = 0;

	return argp;
}
