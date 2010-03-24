#include <mm.h>
#include <string.h>
#include <ros/mman.h>
#include <kmalloc.h>
#include <syscall.h>
#include <elf.h>
#include <pmap.h>

struct elf_info
{
	long entry;
	long highest_addr;
	long phdr;
	int phnum;
	int dynamic;
	char interp[256];
};

int load_one_elf(struct proc* p, int fd, int pgoffset, struct elf_info* ei)
{
	int ret = -1;
	ei->phdr = -1;
	ei->dynamic = 0;
	ei->highest_addr = 0;

	char* elf = (char*)kmalloc(PGSIZE,0);
	if(!elf || read_page(p,fd,PADDR(elf),0) == -1)
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
				if(mmap(p, memstart+pgoffset*PGSIZE, filesz,
				        PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED,
				        fd, filestart/PGSIZE) == MAP_FAILED)
					goto fail;

			filesz = ROUNDUP(filesz,PGSIZE);
			if(filesz < memsz)
				if(mmap(p, memstart+filesz+pgoffset*PGSIZE, memsz-filesz,
				        PROT_READ|PROT_WRITE|PROT_EXEC, MAP_FIXED|MAP_ANON,
				        -1, 0) == MAP_FAILED)
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

int load_elf(struct proc* p, const char* fn)
{
	struct elf_info ei,interp_ei;
	int fd = open_file(p,fn,0,0);
	if(fd == -1 || load_one_elf(p,fd,0,&ei))
		return -1;
	close_file(p,fd);

	if(ei.dynamic)
	{
		// plzplzplz let us use the stack and PADDR()
		char* str = kmalloc(sizeof(ei.interp),0);
		if(!str)
			return -1;
		memcpy(str,ei.interp,sizeof(ei.interp));
		int fd2 = open_file(p,str,0,0);
		kfree(str);

		if(fd2 == -1 || load_one_elf(p,fd2,1,&interp_ei))
			return -1;
		close_file(p,fd2);

		// fill in info for dynamic linker
		elf_aux_t auxp[] = {{ELF_AUX_PHDR,ei.phdr},
		                    {ELF_AUX_PHENT,sizeof(proghdr_t)},
		                    {ELF_AUX_PHNUM,ei.phnum},
		                    {ELF_AUX_ENTRY,ei.entry},
		                    {0,0}};

		// put auxp after argv, envp in procinfo
		int auxp_pos = -1;
		for(int i = 0, zeros = 0; i < PROCINFO_MAX_ARGP; i++)
			if(p->env_procinfo->argp[i] == NULL)
				if(++zeros == 2)
					auxp_pos = i+1;
		if(auxp_pos == -1 ||
		   auxp_pos+sizeof(auxp)/sizeof(char*) >= PROCINFO_MAX_ARGP)
			return -1;
		memcpy(p->env_procinfo->argp+auxp_pos,auxp,sizeof(auxp));
	}

	uintptr_t core0_entry = ei.dynamic ? interp_ei.entry : ei.entry;
	proc_init_trapframe(&p->env_tf,0,core0_entry,USTACKTOP);
	p->env_entry = ei.entry;

	uintptr_t stacksz = USTACK_NUM_PAGES*PGSIZE;
	if(mmap(p,USTACKTOP-stacksz,stacksz,PROT_READ|PROT_WRITE,
	        MAP_FIXED|MAP_ANON,-1,0) == MAP_FAILED)
		return -1;

	// Set the heap bottom and top to just past where the text 
	// region has been loaded
	p->heap_bottom = (void*)ei.highest_addr;
	p->heap_top = p->heap_bottom;

	return 0;
}

