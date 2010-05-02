#include <mm.h>
#include <frontend.h>
#include <string.h>
#include <ros/mman.h>
#include <kmalloc.h>
#include <syscall.h>
#include <elf.h>
#include <pmap.h>

typedef struct
{
	long entry;
	long highest_addr;
	long phdr;
	int phnum;
	int dynamic;
	char interp[256];
} elf_info_t;

static int
load_one_elf(struct proc* p, struct file* f, int pgoffset, elf_info_t* ei)
{
	int ret = -1;
	ei->phdr = -1;
	ei->dynamic = 0;
	ei->highest_addr = 0;

	char* elf = (char*)kmalloc(PGSIZE,0);
	if(!elf || file_read_page(f,PADDR(elf),0) == -1)
		goto fail;

	elf_t* elfhdr = (elf_t*)elf;
	proghdr_t* proghdrs = (proghdr_t*)(elf+elfhdr->e_phoff);
	if(elfhdr->e_phoff+elfhdr->e_phnum*sizeof(proghdr_t) > PGSIZE)
		goto fail;
	if(elfhdr->e_phentsize != sizeof(proghdr_t))
		goto fail;

	for(int i = 0; i < elfhdr->e_phnum; i++)
	{
		proghdr_t* ph = proghdrs+i;
		if(ph->p_type == ELF_PROG_PHDR)
			ei->phdr = ph->p_va;
		if(ph->p_type == ELF_PROG_INTERP)
		{
			int maxlen = MIN(PGSIZE-ph->p_offset,sizeof(ei->interp));
			int len = strnlen(elf+ph->p_offset,maxlen);
			if(len < maxlen)
			{
				memcpy(ei->interp,elf+ph->p_offset,maxlen+1);
				ei->dynamic = 1;
			}
			else
				goto fail;
		}

		if(ph->p_type == ELF_PROG_LOAD && ph->p_memsz)
		{
			if(ph->p_align % PGSIZE)
				goto fail;
			if(ph->p_offset % PGSIZE != ph->p_va % PGSIZE)
				goto fail;

			uintptr_t filestart = ROUNDDOWN(ph->p_offset,PGSIZE);
			uintptr_t fileend = ph->p_offset+ph->p_filesz;
			uintptr_t filesz = fileend-filestart;

			uintptr_t memstart = ROUNDDOWN(ph->p_va,PGSIZE);
			uintptr_t memend = ROUNDUP(ph->p_va + ph->p_memsz,PGSIZE);
			uintptr_t memsz = memend-memstart;
			if(memend > ei->highest_addr)
				ei->highest_addr = memend;

			// mmap will zero the rest of the page if filesz % PGSIZE != 0
			if(filesz)
				// TODO: waterman, figure out proper permissions
				if(do_mmap(p, memstart+pgoffset*PGSIZE, filesz,
				           PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED,
				           f, filestart/PGSIZE) == MAP_FAILED)
					goto fail;

			filesz = ROUNDUP(filesz,PGSIZE);
			if(filesz < memsz)
				if(do_mmap(p, memstart+filesz+pgoffset*PGSIZE, memsz-filesz,
				           PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED|MAP_ANON,
				           NULL, 0) == MAP_FAILED)
					goto fail;
		}
	}

	ei->entry = elfhdr->e_entry + pgoffset*PGSIZE;
	ei->phnum = elfhdr->e_phnum;

	ret = 0;
fail:
	kfree(elf);
	return ret;
}

int load_elf(struct proc* p, struct file* f)
{
	elf_info_t ei,interp_ei;
	if(load_one_elf(p,f,0,&ei))
		return -1;

	if(ei.dynamic)
	{
		struct file* interp = file_open(ei.interp,0,0);
		if(interp == NULL || load_one_elf(p,interp,1,&interp_ei))
			return -1;
		file_decref(interp);

		// fill in info for dynamic linker
		elf_aux_t auxp[] = {{ELF_AUX_PHDR,ei.phdr},
		                    {ELF_AUX_PHENT,sizeof(proghdr_t)},
		                    {ELF_AUX_PHNUM,ei.phnum},
		                    {ELF_AUX_ENTRY,ei.entry},
		                    #ifdef __sparc_v8__
		                    {ELF_AUX_HWCAP,ELF_HWCAP_SPARC_FLUSH},
		                    #endif
		                    {0,0}};

		// put auxp after argv, envp in procinfo
		int auxp_pos = -1;
		for(int i = 0, zeros = 0; i < PROCINFO_MAX_ARGP; i++)
			if(p->procinfo->argp[i] == NULL)
				if(++zeros == 2)
					auxp_pos = i+1;
		if(auxp_pos == -1 ||
		   auxp_pos+sizeof(auxp)/sizeof(char*) >= PROCINFO_MAX_ARGP)
			return -1;
		memcpy(p->procinfo->argp+auxp_pos,auxp,sizeof(auxp));
	}

	uintptr_t core0_entry = ei.dynamic ? interp_ei.entry : ei.entry;
	proc_init_trapframe(&p->env_tf,0,core0_entry,USTACKTOP);
	p->env_entry = ei.entry;

	// map in stack using POPULATE (because SPARC requires it)
	uintptr_t stacksz = USTACK_NUM_PAGES*PGSIZE;
	if(do_mmap(p, USTACKTOP-stacksz, stacksz, PROT_READ | PROT_WRITE,
	           MAP_FIXED | MAP_ANONYMOUS | MAP_POPULATE, NULL, 0) == MAP_FAILED)
		return -1;

	// Set the heap bottom and top to just past where the text 
	// region has been loaded
	p->heap_top = (void*)ei.highest_addr;
	p->procinfo->heap_bottom = p->heap_top;

	return 0;
}

