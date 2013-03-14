/* See COPYRIGHT for copyright information. */

#ifndef ROS_PROCINFO_H
#define ROS_PROCINFO_H

#include <ros/memlayout.h>
#include <ros/common.h>
#include <ros/resource.h>
#include <ros/atomic.h>
#include <ros/arch/arch.h>
#include <string.h>

#define PROCINFO_MAX_ARGP 32
#define PROCINFO_ARGBUF_SIZE 3072

#ifdef ROS_KERNEL
#include <sys/queue.h>
#endif /* ROS_KERNEL */

/* Not necessary to expose all of this, but it doesn't hurt, and is convenient
 * for the kernel.  Need to do some acrobatics for the TAILQ_ENTRY. */
struct vcore;
struct vcore {
#ifdef ROS_KERNEL
	TAILQ_ENTRY(vcore)	list;
#else /* userspace */
	void				*dummy_ptr1;
	void				*dummy_ptr2;
#endif /* ROS_KERNEL */
	uint32_t			pcoreid;
	bool				valid;
	uint32_t			nr_preempts_sent;	/* these two differ when a preempt*/
	uint32_t			nr_preempts_done;	/* is in flight. */
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
	uint64_t timing_overhead;
	void *heap_bottom;
	/* for traditional forks, these two need to be memcpy'd over: */
	char *argp[PROCINFO_MAX_ARGP];
	char argbuf[PROCINFO_ARGBUF_SIZE];
	/* glibc relies on stuff above this point.  if you change it, you need to
	 * rebuild glibc. */
	bool is_mcp;			/* is in multi mode */
	unsigned long 		res_grant[MAX_NUM_RESOURCES];
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
	int i;
	for(i = 0; i < nargv; i++)
	{
		int len = strlen(argv[i])+1;
		if(pos+len > PROCINFO_ARGBUF_SIZE)
			return -1;
		p->argp[i] = ((procinfo_t*)UINFO)->argbuf+pos;
		memcpy(p->argbuf+pos,argv[i],len);
		pos += len;
	}
	p->argp[nargv] = 0;

	for(i = 0; i < nenvp; i++)
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

#include <ros/common.h>
#include <ros/atomic.h>
#include <ros/syscall.h>

/* Figure out what your vcoreid is from your pcoreid and procinfo.  Only low
 * level or debugging code should call this. */
static inline uint32_t __get_vcoreid_from_procinfo(void)
{
	/* The assumption is that any IPIs/KMSGs would knock userspace into the
	 * kernel before it could read the closing of the seqctr.  Put another way,
	 * there is a 'memory barrier' between the IPI write and the seqctr write.
	 * I think this is true. */
	uint32_t kpcoreid, kvcoreid;
	seq_ctr_t old_seq;
	do {
		cmb();
		old_seq = __procinfo.coremap_seqctr;
		kpcoreid = __ros_syscall(SYS_getpcoreid, 0, 0, 0, 0, 0, 0, NULL);
		if (!__procinfo.pcoremap[kpcoreid].valid)
			continue;
		kvcoreid = __procinfo.pcoremap[kpcoreid].vcoreid;
	} while (seqctr_retry(old_seq, __procinfo.coremap_seqctr));
	return kvcoreid;
}

static inline uint32_t __get_vcoreid(void)
{
	/* since sys_getvcoreid could lie (and might never change) */
	return __get_vcoreid_from_procinfo();
}

#endif /* ifndef ROS_KERNEL */

#endif // !ROS_PROCDATA_H
