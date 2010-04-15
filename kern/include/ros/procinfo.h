/* See COPYRIGHT for copyright information. */

#ifndef ROS_PROCINFO_H
#define ROS_PROCINFO_H

#include <ros/memlayout.h>
#include <ros/common.h>
#include <ros/atomic.h>
#include <ros/arch/arch.h>

#define PROCINFO_MAX_ARGP 32
#define PROCINFO_ARGBUF_SIZE 3072


/* Not necessary to expose all of this, but it doesn't hurt, and is convenient
 * for the kernel. */
struct vcore {
	uint32_t			pcoreid;
	bool				valid;
	bool				preempt_served;
	uint64_t			preempt_pending;
};

struct pcore {
	uint32_t			vcoreid;
	bool 				valid;
};

typedef struct procinfo {
	pid_t pid;
	pid_t ppid;
	size_t max_vcores;
	uint64_t tsc_freq;
	void* heap_bottom;
	char* argp[PROCINFO_MAX_ARGP];
	char argbuf[PROCINFO_ARGBUF_SIZE];
	/* glibc relies on stuff above this point.  if you change it, you need to
	 * rebuild glibc. */
	struct vcore		vcoremap[MAX_NUM_CPUS];
	uint32_t			num_vcores;
	struct pcore		pcoremap[MAX_NUM_CPUS];
	seq_ctr_t			coremap_seqctr;
} procinfo_t;
#define PROCINFO_NUM_PAGES  ((sizeof(procinfo_t)-1)/PGSIZE + 1)	

static int
procinfo_pack_args(procinfo_t* p, char* const* argv, char* const* envp)
{
	int nargv = 0, nenvp = 0;
	if(argv) while(argv[nargv]) nargv++;
	if(envp) while(envp[nenvp]) nenvp++;

	if(nargv+nenvp+2 > PROCINFO_MAX_ARGP)
		return -1;

	int pos = 0;
	for(int i = 0; i < nargv; i++)
	{
		int len = strlen(argv[i])+1;
		if(pos+len > PROCINFO_ARGBUF_SIZE)
			return -1;
		p->argp[i] = ((procinfo_t*)UINFO)->argbuf+pos;
		memcpy(p->argbuf+pos,argv[i],len);
		pos += len;
	}
	p->argp[nargv] = 0;

	for(int i = 0; i < nenvp; i++)
	{
		int len = strlen(envp[i])+1;
		if(pos+len > PROCINFO_ARGBUF_SIZE)
			return -1;
		p->argp[nargv+1+i] = ((procinfo_t*)UINFO)->argbuf+pos;
		memcpy(p->argbuf+pos,envp[i],len);
		pos += len;
	}
	p->argp[nargv+nenvp+1] = 0;
	
	return 0;
}

// this is how user programs access the procinfo page
#ifndef ROS_KERNEL
# define __procinfo (*(procinfo_t*)UINFO)
#endif

#endif // !ROS_PROCDATA_H
