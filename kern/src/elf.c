#include <mm.h>
#include <frontend.h>
#include <string.h>
#include <kmalloc.h>
#include <syscall.h>
#include <elf.h>
#include <pmap.h>
#include <smp.h>
#include <arch/arch.h>

#ifdef CONFIG_64BIT
# define elf_field(obj, field) (elf64 ? (obj##64)->field : (obj##32)->field)
#else
# define elf_field(obj, field) ((obj##32)->field)
#endif

/* Check if the file is valid elf file (i.e. by checking for ELF_MAGIC in the
 * header) */
bool is_valid_elf(struct file *f)
{
	elf64_t h;
	off64_t o = 0;
	struct proc *c = switch_to(0);

	if (f->f_op->read(f, (char*)&h, sizeof(elf64_t), &o) != sizeof(elf64_t)) {
		goto fail;
	}
	if (h.e_magic != ELF_MAGIC) {
		goto fail;
	}
success:
	switch_back(0, c);
	return TRUE;
fail:
	switch_back(0, c);
	return FALSE;
}

/* We need the writable flag for ld.  Even though the elf header says it wants
 * RX (and not W) for its main program header, it will page fault (eip 56f0,
 * 46f0 after being relocated to 0x1000, va 0x20f4). */
static int load_one_elf(struct proc *p, struct file *f, uintptr_t pgoffset,
                        elf_info_t *ei, bool writable)
{
	int ret = -1;
	ei->phdr = -1;
	ei->dynamic = 0;
	ei->highest_addr = 0;
	off64_t f_off = 0;
	void* phdrs = 0;
	int mm_perms, mm_flags = MAP_FIXED;
	
	/* When reading on behalf of the kernel, we need to make sure no proc is
	 * "current".  This is a bit ghetto (TODO: KFOP) */
	struct proc *old_proc = switch_to(0);

	/* Read in ELF header. */
	elf64_t elfhdr_storage;
	elf32_t* elfhdr32 = (elf32_t*)&elfhdr_storage;
	elf64_t* elfhdr64 = &elfhdr_storage;
	if (f->f_op->read(f, (char*)elfhdr64, sizeof(elf64_t), &f_off)
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
	#ifdef CONFIG_X86_64
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
	if (!phdrs || f->f_op->read(f, phdrs, e_phnum * phsz, &f_off) !=
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
		mm_flags = MAP_FIXED | (p_flags & ELF_PROT_WRITE ? MAP_PRIVATE : 0);

		if (p_type == ELF_PROG_PHDR)
			ei->phdr = p_va;
		else if (p_type == ELF_PROG_INTERP) {
			f_off = p_offset;
			ssize_t maxlen = sizeof(ei->interp);
			ssize_t bytes = f->f_op->read(f, ei->interp, maxlen, &f_off);
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
			memstart += pgoffset * PGSIZE;

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
					            mm_flags, f, filestart) == MAP_FAILED) {
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
					mm_flags |= MAP_PRIVATE | MAP_POPULATE;

					/* Map the final partial page. */
					uintptr_t last_page = memstart + filesz - partial;
					if (do_mmap(p, last_page, PGSIZE, mm_perms, mm_flags,
					            f, filestart + filesz - partial) == MAP_FAILED) {
						printk("[kernel] load_one_elf: partial mmap failed\n");
						goto fail;
					}

					/* Zero the end of it. */
					pte_t *pte = pgdir_walk(p->env_pgdir, (void*)last_page, 0);
					assert(pte);
					void* last_page_kva = ppn2kva(PTE2PPN(*pte));
					memset(last_page_kva + partial, 0, PGSIZE - partial);

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
		                          MAP_PRIVATE, f, filestart);
		if (phdr_addr == MAP_FAILED) {
			printk("[kernel] load_one_elf: prog header mmap failed\n");
			goto fail;
		}
		ei->phdr = (long)phdr_addr + e_phoff;
	}
	ei->entry = elf_field(elfhdr, e_entry) + pgoffset*PGSIZE;
	ei->phnum = e_phnum;
	ei->elf64 = elf64;
	ret = 0;
	/* Fall-through */
fail:
	if (phdrs)
		kfree(phdrs);
	switch_back(0, old_proc);
	return ret;
}

int load_elf(struct proc* p, struct file* f)
{
	elf_info_t ei, interp_ei;
	if (load_one_elf(p, f, 0, &ei, FALSE))
		return -1;

	if (ei.dynamic) {
		struct file *interp = do_file_open(ei.interp, 0, 0);
		if (!interp)
			return -1;
		/* Load dynamic linker one page into the address space */
		int error = load_one_elf(p, interp, 1, &interp_ei, TRUE);
		kref_put(&interp->f_kref);
		if (error)
			return -1;
	}

	// fill in auxiliary info for dynamic linker/runtime
	elf_aux_t auxp[] = {{ELF_AUX_PHDR, ei.phdr},
	                    {ELF_AUX_PHENT, sizeof(proghdr32_t)},
	                    {ELF_AUX_PHNUM, ei.phnum},
	                    {ELF_AUX_ENTRY, ei.entry},
	                    {0, 0}};

	// put auxp after argv, envp in procinfo
	int auxp_pos = -1;
	for (int i = 0, zeros = 0; i < PROCINFO_MAX_ARGP; i++)
		if (p->procinfo->argp[i] == NULL)
			if (++zeros == 2)
				auxp_pos = i + 1;
	if (auxp_pos == -1 ||
	    auxp_pos + sizeof(auxp) / sizeof(char*) >= PROCINFO_MAX_ARGP)
		return -1;
	memcpy(p->procinfo->argp+auxp_pos,auxp,sizeof(auxp));

	uintptr_t core0_entry = ei.dynamic ? interp_ei.entry : ei.entry;
	proc_init_ctx(&p->scp_ctx, 0, core0_entry, USTACKTOP, 0);
	p->env_entry = ei.entry;

	int flags = MAP_FIXED | MAP_ANONYMOUS;
	uintptr_t stacksz = USTACK_NUM_PAGES*PGSIZE;
	if (do_mmap(p, USTACKTOP-stacksz, stacksz, PROT_READ | PROT_WRITE,
	            flags, NULL, 0) == MAP_FAILED)
		return -1;

	// Set the heap bottom and top to just past where the text 
	// region has been loaded
	p->heap_top = (void*)ei.highest_addr;
	p->procinfo->heap_bottom = p->heap_top;

	return 0;
}

