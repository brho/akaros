#include <stdbool.h>
#include <errno.h>
#include <hart.h>
#include <parlib.h>
#include <unistd.h>
#include <stdlib.h>

static size_t _current_harts = 1;
static hart_lock_t _hart_lock = HART_LOCK_INIT;

extern void** hart_thread_control_blocks;

static void hart_free_tls(int id)
{
	extern void _dl_deallocate_tls (void *tcb, bool dealloc_tcb);
	if(hart_thread_control_blocks[id])
	{
		_dl_deallocate_tls(hart_thread_control_blocks[id],true);
		hart_thread_control_blocks[id] = 0;
	}
}

static int hart_allocate_tls(int id)
{
	extern void *_dl_allocate_tls (void *mem);
	// instead of freeing any old tls that may be present, and then
	// reallocating it, we could instead just reinitialize it.
	hart_free_tls(id);
	if((hart_thread_control_blocks[id] = _dl_allocate_tls(NULL)) == NULL)
	{
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

#define HART_STACK_SIZE (32*1024)

static void hart_free_stack(int id)
{
	// don't actually free stacks
}

static int hart_allocate_stack(int id)
{
	if(__procdata.stack_pointers[id])
		return 0; // reuse old stack

	if(!(__procdata.stack_pointers[id] = (uintptr_t)malloc(HART_STACK_SIZE)))
	{
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

static int hart_init()
{
	static int initialized = 0;
	if(initialized)
		return 0;

	hart_thread_control_blocks = (void**)calloc(hart_max_harts(),sizeof(void*));

	if(!hart_thread_control_blocks)
	{
		free(hart_thread_control_blocks);
		errno = ENOMEM;
		return -1;
	}

	initialized = 1;
	return 0;
}

int hart_request(size_t k)
{
	int ret = -1;
	size_t i,j;

	if(hart_init() < 0)
		return -1;

	hart_lock_lock(&_hart_lock);

	if(k < 0 || _current_harts+k > hart_max_harts())
	{
		errno = EAGAIN;
		goto out;
	}

	for(i = _current_harts, j = 0; i < _current_harts+k; i++, j++)
	{
		if(hart_allocate_stack(i) || hart_allocate_tls(i))
			goto fail;
	}

	if((ret = sys_resource_req(0,_current_harts+k,0)) == 0)
	{
		_current_harts += k;
		goto out;
	}

fail:
	for(i = _current_harts; i < _current_harts+k; i++)
	{
		hart_free_tls(i);
		hart_free_stack(i);
	}

out:
	hart_lock_unlock(&_hart_lock);
	return ret;
}

void hart_yield()
{
	int id = hart_self();

	hart_lock_lock(&_hart_lock);
	_current_harts--;
	if(_current_harts == 0)
		exit(0);
	hart_lock_unlock(&_hart_lock);

	sys_yield();
}

size_t hart_max_harts()
{
	return __procinfo.max_harts < HART_MAX_MAX_HARTS ? __procinfo.max_harts : HART_MAX_MAX_HARTS;
}

size_t hart_current_harts()
{
	return _current_harts;
}

// MCS locks!!
void hart_lock_init(hart_lock_t* lock)
{
	memset(lock,0,sizeof(hart_lock_t));
}

static inline hart_lock_qnode_t* hart_qnode_swap(hart_lock_qnode_t** addr, hart_lock_qnode_t* val)
{
	return (hart_lock_qnode_t*)hart_swap((int*)addr,(int)val);
}

void hart_lock_lock(hart_lock_t* lock)
{
	hart_lock_qnode_t* qnode = &lock->qnode[hart_self()];
	qnode->next = 0;
	hart_lock_qnode_t* predecessor = hart_qnode_swap(&lock->lock,qnode);
	if(predecessor)
	{
		qnode->locked = 1;
		predecessor->next = qnode;
		while(qnode->locked);
	}
}

void hart_lock_unlock(hart_lock_t* lock)
{
	hart_lock_qnode_t* qnode = &lock->qnode[hart_self()];
	if(qnode->next == 0)
	{
		hart_lock_qnode_t* old_tail = hart_qnode_swap(&lock->lock,0);
		if(old_tail == qnode)
			return;

		hart_lock_qnode_t* usurper = hart_qnode_swap(&lock->lock,old_tail);
		while(qnode->next == 0);
		if(usurper)
			usurper->next = qnode->next;
		else
			qnode->next->locked = 0;
	}
	else
		qnode->next->locked = 0;
}

// MCS dissemination barrier!
int hart_barrier_init(hart_barrier_t* b, size_t np)
{
	if(np > hart_max_harts())
		return -1;
	b->allnodes = (hart_dissem_flags_t*)malloc(np*sizeof(hart_dissem_flags_t));
	memset(b->allnodes,0,np*sizeof(hart_dissem_flags_t));
	b->nprocs = np;

	b->logp = (np & (np-1)) != 0;
	while(np >>= 1)
		b->logp++;

	size_t i,k;
	for(i = 0; i < b->nprocs; i++)
	{
		b->allnodes[i].parity = 0;
		b->allnodes[i].sense = 1;

		for(k = 0; k < b->logp; k++)
		{
			size_t j = (i+(1<<k)) % b->nprocs;
			b->allnodes[i].partnerflags[0][k] = &b->allnodes[j].myflags[0][k];
			b->allnodes[i].partnerflags[1][k] = &b->allnodes[j].myflags[1][k];
		} 
	}

	return 0;
}

void hart_barrier_wait(hart_barrier_t* b, size_t pid)
{
	hart_dissem_flags_t* localflags = &b->allnodes[pid];
	size_t i;
	for(i = 0; i < b->logp; i++)
	{
		*localflags->partnerflags[localflags->parity][i] = localflags->sense;
		while(localflags->myflags[localflags->parity][i] != localflags->sense);
	}
	if(localflags->parity)
		localflags->sense = 1-localflags->sense;
	localflags->parity = 1-localflags->parity;
}

int
hart_self()
{
	// defined in ros/arch/hart.h
	return __hart_self();
}

int
hart_swap(int* addr, int val)
{
	return __hart_swap(addr,val);
}

void
hart_relax()
{
	__hart_relax();
}

