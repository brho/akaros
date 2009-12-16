#include <hart.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <parlib.h>

static size_t _current_harts = 1;
static hart_lock_t _hart_lock = HART_LOCK_INIT;

static void _hart_init()
{
	static int initialized = 0;
	if(initialized)
		return;

	initialized = 1;

	extern void **stack_ptr_array, **tls_array;
	stack_ptr_array = (void**)calloc(hart_max_harts(),sizeof(void*));
	tls_array = (void**)calloc(hart_max_harts(),sizeof(void*));

	if(stack_ptr_array == NULL || tls_array == NULL)
	{
		fputs("Harts initialization ran out of memory!\n",stderr);
		abort();
	}	
}

error_t hart_request(size_t k)
{
	size_t i,j;
	const int user_stack_size = 1024*1024, tls_size = 1024*1024;

	extern void** stack_ptr_array;
	extern void** tls_array;

	_hart_init();

	hart_lock_lock(&_hart_lock);

	if(k < 0 || _current_harts+k > hart_max_harts())
		return -1;

	char* stack = (char*)calloc(user_stack_size+tls_size,k);
	if(stack == NULL)
	{
		hart_lock_unlock(&_hart_lock);
		return -ENOMEM;
	}

	for(i = _current_harts, j = 0; i < _current_harts+k; i++, j++)
	{
		stack_ptr_array[i] = stack + (j+1)*user_stack_size+j*tls_size;
		tls_array[i] = stack_ptr_array[i]+tls_size;
	}

	error_t ret;
	if((ret = sys_resource_req(0,_current_harts+k,0)) == 0)
	{
		_current_harts += k;
		hart_lock_unlock(&_hart_lock);
		return 0;
	}

	free(stack);
	for(i = _current_harts; i < _current_harts+k; i++)
		stack_ptr_array[i] = tls_array[i] = 0;

	hart_lock_unlock(&_hart_lock);
	return ret;
}

void hart_yield()
{
	hart_lock_lock(&_hart_lock);
	_current_harts--;
	hart_lock_unlock(&_hart_lock);
	syscall(SYS_yield,0,0,0,0,0);
}

size_t hart_max_harts()
{
	return procinfo.max_harts < HART_MAX_MAX_HARTS ? procinfo.max_harts : HART_MAX_MAX_HARTS;
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
error_t hart_barrier_init(hart_barrier_t* b, size_t np)
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

