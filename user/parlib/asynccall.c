#include <stdlib.h>

#include <ros/common.h>
#include <ros/syscall.h>
#include <ros/ring_syscall.h>
#include <ros/sysevent.h>
#include <arc.h>
#include <errno.h>
#include <arch/arch.h>
#include <sys/param.h>
#include <arch/atomic.h>
#include <vcore.h>

syscall_desc_pool_t syscall_desc_pool;
async_desc_pool_t async_desc_pool;
async_desc_t* current_async_desc;

struct arsc_channel global_ac;

void init_arc(struct arsc_channel* ac)
{
	// Set up the front ring for the general syscall ring
	// and the back ring for the general sysevent ring
	mcs_lock_init(&ac->aclock);
	ac->ring_page = (syscall_sring_t*)sys_init_arsc();

	FRONT_RING_INIT(&ac->sysfr, ac->ring_page, SYSCALLRINGSIZE);
	//BACK_RING_INIT(&syseventbackring, &(__procdata.syseventring), SYSEVENTRINGSIZE);
	//TODO: eventually rethink about desc pools, they are here but no longer necessary
	POOL_INIT(&syscall_desc_pool, MAX_SYSCALLS);
	POOL_INIT(&async_desc_pool, MAX_ASYNCCALLS);
}

// Wait on all syscalls within this async call.  TODO - timeout or something?
int waiton_group_call(async_desc_t* desc, async_rsp_t* rsp)
{
	syscall_rsp_t syscall_rsp;
	syscall_desc_t* d;
	int retval = 0;
	int err = 0;
	if (!desc) {
		errno = EINVAL;
		return -1;
	}

	while (!(TAILQ_EMPTY(&desc->syslist))) {
		d = TAILQ_FIRST(&desc->syslist);
		err = waiton_syscall(d);
		// TODO: processing the retval out of rsp here.  might be specific to
		// the async call.  do we want to accumulate?  return any negative
		// values?  depends what we want from the return value, so we might
		// have to pass in a function that is used to do the processing and
		// pass the answer back out in rsp.
		//rsp->retval += syscall_rsp.retval; // For example
		retval = MIN(retval, err);
		// remove from the list and free the syscall desc
		TAILQ_REMOVE(&desc->syslist, d, next);
		POOL_PUT(&syscall_desc_pool, d);
	}
	// run a cleanup function for this desc, if available
	if (desc->cleanup)
		desc->cleanup(desc->data);
	// free the asynccall desc
	POOL_PUT(&async_desc_pool, desc);
	return err;
}

// Finds a free async_desc_t, on which you can wait for a series of syscalls
async_desc_t* get_async_desc(void)
{
	async_desc_t* desc = POOL_GET(&async_desc_pool);
	if (desc) {
		// Clear out any data that was in the old desc
		memset(desc, 0, sizeof(*desc));
		TAILQ_INIT(&desc->syslist);
	}
	return desc;
}

// Finds a free sys_desc_t, on which you can wait for a specific syscall, and
// binds it to the group desc.
syscall_desc_t* get_sys_desc(async_desc_t* desc)
{
	syscall_desc_t* d = POOL_GET(&syscall_desc_pool);
	if (d) {
		// Clear out any data that was in the old desc
		memset(d, 0, sizeof(*d));
    	TAILQ_INSERT_TAIL(&desc->syslist, d, next);
	}
	return d;
}

// Gets an async and a sys desc, with the sys bound to async.  Also sets
// current_async_desc.  This is meant as an easy wrapper when there is only one
// syscall for an async call.
int get_all_desc(async_desc_t** a_desc, syscall_desc_t** s_desc)
{
	assert(a_desc && s_desc);
	if ((current_async_desc = get_async_desc()) == NULL){
		errno = EBUSY;
		return -1;
	}
	*a_desc = current_async_desc;
	if ((*s_desc = get_sys_desc(current_async_desc)))
		return 0;
	// in case we could get an async, but not a syscall desc, then clean up.
	POOL_PUT(&async_desc_pool, current_async_desc);
	current_async_desc = NULL;
	errno = EBUSY;
	return -1;
}

// This runs one syscall instead of a group. 

// TODO: right now there is one channel (remote), in the future, the caller
// may specify local which will cause it to give up the core to do the work.
// creation of additional remote channel also allows the caller to prioritize
// work, because the default policy for the kernel is to roundrobin between them.
int async_syscall(arsc_channel_t* chan, syscall_req_t* req, syscall_desc_t** desc_ptr2)
{
	// Note that this assumes one global frontring (TODO)
	// abort if there is no room for our request.  ring size is currently 64.
	// we could spin til it's free, but that could deadlock if this same thread
	// is supposed to consume the requests it is waiting on later.
	syscall_desc_t* desc = malloc(sizeof (syscall_desc_t));
	desc->channel = chan;
	syscall_front_ring_t *fr = &(desc->channel->sysfr);
	//TODO: can do it locklessly using CAS, but could change with local async calls
	struct mcs_lock_qnode local_qn = {0};
	mcs_lock_lock(&(chan->aclock), &local_qn);
	if (RING_FULL(fr)) {
		errno = EBUSY;
		return -1;
	}
	// req_prod_pvt comes in as the previously produced item.  need to
	// increment to the next available spot, which is the one we'll work on.
	// at some point, we need to listen for the responses.
	desc->idx = ++(fr->req_prod_pvt);
	syscall_req_t* r = RING_GET_REQUEST(fr, desc->idx);
	// CAS on the req->status perhaps
	req->status = REQ_alloc;

	memcpy(r, req, sizeof(syscall_req_t));
	r->status = REQ_ready;
	// push our updates to syscallfrontring.req_prod_pvt
	// note: it is ok to push without protection since it is atomic and kernel
	// won't process any requests until they are marked REQ_ready (also atomic)
	RING_PUSH_REQUESTS(fr);
	//cprintf("DEBUG: sring->req_prod: %d, sring->rsp_prod: %d\n", 
	mcs_lock_unlock(&desc->channel->aclock, &local_qn);
	*desc_ptr2 = desc;
	return 0;
}
// Default convinence wrapper before other method of posting calls are available

syscall_desc_t* arc_call(long int num, ...)
{
  	va_list vl;
  	va_start(vl,num);
	struct syscall *p_sysc = malloc(sizeof (struct syscall));
	syscall_desc_t* desc;
	if (p_sysc == NULL) {
		errno = ENOMEM;
		return 0;
	}
	p_sysc->num = num;
  	p_sysc->arg0 = va_arg(vl,long int);
  	p_sysc->arg1 = va_arg(vl,long int);
  	p_sysc->arg2 = va_arg(vl,long int);
  	p_sysc->arg3 = va_arg(vl,long int);
  	p_sysc->arg4 = va_arg(vl,long int);
  	p_sysc->arg5 = va_arg(vl,long int);
  	va_end(vl);
	syscall_req_t arc = {REQ_alloc,NULL, NULL, p_sysc};
	async_syscall(&SYS_CHANNEL, &arc, &desc);
	printf ( "%d pushed at %p \n", desc);
	return desc;
}

// consider a timeout too
// Wait until arsc returns, caller provides rsp buffer.
// eventually change this to return ret_val, set errno

// What if someone calls waiton the same desc several times?
int waiton_syscall(syscall_desc_t* desc)
{
	int retval = 0;
	if (desc == NULL || desc->channel == NULL){
		errno = EFAIL;
		return -1;
	}
	// Make sure we were given a desc with a non-NULL frontring.  This could
	// happen if someone forgot to check the error code on the paired syscall.
	syscall_front_ring_t *fr =  &desc->channel->sysfr;
	
	if (!fr){
		errno = EFAIL;
		return -1;
	}
	printf("waiting %d\n", vcore_id());
	syscall_rsp_t* rsp = RING_GET_RESPONSE(fr, desc->idx);

	// ignoring the ring push response from the kernel side now
	while (atomic_read(&rsp->sc->flags) != SC_DONE)
		cpu_relax();
	// memcpy(rsp, rsp_inring, sizeof(*rsp));
	
    // run a cleanup function for this desc, if available
    if (rsp->cleanup)
    	rsp->cleanup(rsp->data);
	if (RSP_ERRNO(rsp)){
		errno = RSP_ERRNO(rsp);
		retval = -1;
	} else 
		retval =  RSP_RESULT(rsp); 
	atomic_inc((atomic_t*) &(fr->rsp_cons));
	return retval;
}


