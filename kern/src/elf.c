#include <mm.h>
#include <frontend.h>
#include <string.h>
#include <kmalloc.h>
#include <syscall.h>
#include <elf.h>
#include <pmap.h>
#include <smp.h>
#include <arch/arch.h>

#ifdef KERN64
# define elf_field(obj, field) (elf64 ? (obj##64)->field : (obj##32)->field)
#else
# define elf_field(obj, field) ((obj##32)->field)
#endif

static int load_one_elf(struct proc *p, struct file *f, uintptr_t pgoffset,
                        elf_info_t *ei)
{
	int ret = -1;
	ei->phdr = -1;
	ei->dynamic = 0;
	ei->highest_addr = 0;
	off_t f_off = 0;
	void* phdrs = 0;
	
	/* When reading on behalf of the kernel, we need to make sure no proc is
	 * "current".  This is a bit ghetto (TODO: KFOP) */
	struct proc *cur_proc = current;
	current = 0;

	/* Read in ELF header. */
	elf64_t elfhdr_storage;
	elf32_t* elfhdr32 = (elf32_t*)&elfhdr_storage;
	elf64_t* elfhdr64 = &elfhdr_storage;
	if (f->f_op->read(f, (char*)elfhdr64, sizeof(elf64_t), &f_off) == -1)
		goto fail;

	bool elf32 = elfhdr32->e_ident[ELF_IDENT_CLASS] == ELFCLASS32;
	bool elf64 = elfhdr64->e_ident[ELF_IDENT_CLASS] == ELFCLASS64;
	if (elf64 == elf32)
		goto fail;
	#ifndef KERN64
	if(elf64)
		goto fail;
	#endif

	size_t phsz = elf64 ? sizeof(proghdr64_t) : sizeof(proghdr32_t);
	uint16_t e_phnum = elf_field(elfhdr, e_phnum);
	uint16_t e_phoff = elf_field(elfhdr, e_phoff);

	/* Read in program headers. */
	if (e_phnum > 10000 || e_phoff % (elf32 ? 4 : 8) != 0)
	  goto fail;
	phdrs = kmalloc(e_phnum * phsz, 0);
	f_off = e_phoff;
	if (!phdrs || f->f_op->read(f, phdrs, e_phnum * phsz, &f_off) == -1)
		goto fail;

	int flags = MAP_FIXED | MAP_PRIVATE;

	for (int i = 0; i < e_phnum; i++) {
		proghdr32_t* ph32 = (proghdr32_t*)phdrs + i;
		proghdr64_t* ph64 = (proghdr64_t*)phdrs + i;
		uint16_t p_type = elf_field(ph, p_type);
		uintptr_t p_va = elf_field(ph, p_va);
		uintptr_t p_offset = elf_field(ph, p_offset);
		uintptr_t p_align = elf_field(ph, p_align);
		uintptr_t p_memsz = elf_field(ph, p_memsz);
		uintptr_t p_filesz = elf_field(ph, p_filesz);

		if (p_type == ELF_PROG_PHDR)
			ei->phdr = p_va;
		else if (p_type == ELF_PROG_INTERP) {
			f_off = p_offset;
			ssize_t maxlen = sizeof(ei->interp);
			ssize_t bytes = f->f_op->read(f, ei->interp, maxlen, &f_off);
			if (bytes == -1)
			  goto fail;

			maxlen = MIN(maxlen, bytes);
			if (strnlen(ei->interp, maxlen) == maxlen)
			  goto fail;

			ei->dynamic = 1;
		}
		else if (p_type == ELF_PROG_LOAD && p_memsz) {
			if (p_align % PGSIZE)
				goto fail;
			if (p_offset % PGSIZE != p_va % PGSIZE)
				goto fail;

			uintptr_t filestart = ROUNDDOWN(p_offset, PGSIZE);
			uintptr_t filesz = p_offset + p_filesz - filestart;

			uintptr_t memstart = ROUNDDOWN(p_va, PGSIZE);
			uintptr_t memsz = ROUNDUP(p_va + p_memsz, PGSIZE) - memstart;
			memstart += pgoffset * PGSIZE;

			if (memstart + memsz > ei->highest_addr)
				ei->highest_addr = memstart + memsz;

			/* TODO: figure out proper permissions from the elf */
			int perms = PROT_READ | PROT_WRITE | PROT_EXEC;

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
					if (do_mmap(p, memstart, filesz - partial, perms, flags,
					            f, filestart) == MAP_FAILED)
						goto fail;
				}

				if (partial) {
					/* Map the final partial page. */
					uintptr_t last_page = memstart + filesz - partial;
					int partial_flags = flags | MAP_POPULATE;
					if (do_mmap(p, last_page, PGSIZE, perms, partial_flags,
					            f, filestart + filesz - partial) == MAP_FAILED)
						goto fail;

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
				           perms, flags, NULL, 0) == MAP_FAILED)
					goto fail;
		}
	}
	/* map in program headers anyway if not present in binary.
	 * useful for TLS in static programs. */
	if (ei->phdr == -1) {
		uintptr_t filestart = ROUNDDOWN(e_phoff, PGSIZE);
		uintptr_t filesz = e_phoff + (e_phnum * phsz) - filestart;
		void *phdr_addr = do_mmap(p, 0, filesz, PROT_READ,
		                          flags & ~MAP_FIXED, f, filestart);
		if (phdr_addr == MAP_FAILED)
			goto fail;
		ei->phdr = (long)phdr_addr + e_phoff;
	}
	ei->entry = elf_field(elfhdr, e_entry) + pgoffset*PGSIZE;
	ei->phnum = e_phnum;
	ei->elf64 = elf64;
	ret = 0;
fail:
	if (phdrs)
	  kfree(phdrs);
	current = cur_proc;
	return ret;
}

int load_elf(struct proc* p, struct file* f)
{
	elf_info_t ei, interp_ei;
	if (load_one_elf(p, f, 0, &ei))
		return -1;

	if (ei.dynamic) {
		struct file *interp = do_file_open(ei.interp, 0, 0);
		if (!interp)
			return -1;
		/* Load dynamic linker one page into the address space */
		int error = load_one_elf(p, interp, 1, &interp_ei);
		kref_put(&interp->f_kref);
		if (error)
			return -1;
	}

	// fill in auxiliary info for dynamic linker/runtime
	elf_aux_t auxp[] = {{ELF_AUX_PHDR, ei.phdr},
	                    {ELF_AUX_PHENT, sizeof(proghdr32_t)},
	                    {ELF_AUX_PHNUM, ei.phnum},
	                    {ELF_AUX_ENTRY, ei.entry},
	                    #ifdef __sparc_v8__
	                    {ELF_AUX_HWCAP, ELF_HWCAP_SPARC_FLUSH},
	                    #endif
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
	proc_init_trapframe(&p->env_tf,0,core0_entry,USTACKTOP);
	p->env_entry = ei.entry;

	int flags = MAP_FIXED | MAP_ANONYMOUS;
	#ifdef __sparc_v8__
	flags |= MAP_POPULATE; // SPARC stacks must be mapped in
	#endif
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

