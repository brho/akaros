/* See COPYRIGHT for copyright information. */
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/x86.h>
#include <arch/console.h>
#include <arch/apic.h>
#include <error.h>
#include <string.h>
#include <assert.h>
#include <env.h>
#include <pmap.h>
#include <trap.h>
#include <syscall.h>

void syscall_wrapper(struct Trapframe *tf)
{
	env_t* curenv = curenvs[lapic_get_id()];
    curenv->env_tf = *tf;
    tf->tf_regs.reg_eax =
        syscall(curenv,
				tf->tf_regs.reg_eax,
                tf->tf_regs.reg_edx,
                tf->tf_regs.reg_ecx,
                tf->tf_regs.reg_ebx,
                tf->tf_regs.reg_edi,
				0);
    return;
}

//Do absolutely nothing.  Used for profiling.
static void sys_null(env_t* e)
{
	return;
}

// Writes 'val' to 'num_writes' entries of the well-known array in the kernel
// address space.  It's just #defined to be some random 4MB chunk (which ought
// to be boot_alloced or something).  Meant to grab exclusive access to cache
// lines, to simulate doing something useful.
static void sys_cache_buster(env_t* e, uint32_t num_writes, uint32_t val)
{
	#define BUSTER_ADDR 0xd0000000
	#define MAX_WRITES 1048576
	uint32_t* buster = (uint32_t*)BUSTER_ADDR;
	static uint32_t buster_lock = 0;
	
	spin_lock(&buster_lock);
	for (int i = 0; i < MIN(num_writes, MAX_WRITES); i++)
		buster[i] = val;
	spin_unlock(&buster_lock);
}

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(env_t* e, const char *DANGEROUS s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
    char *COUNT(len) _s = user_mem_assert(e, s, len, PTE_U);

	// Print the string supplied by the user.
	cprintf("%.*s", len, _s);
}

// Read a character from the system console.
// Returns the character.
static int
sys_cgetc(env_t* e)
{
	int c;

	// The cons_getc() primitive doesn't wait for a character,
	// but the sys_cgetc() system call does.
	while ((c = cons_getc()) == 0)
		/* do nothing */;

	return c;
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(env_t* e)
{
	return e->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(env_t* e, envid_t envid)
{
	int r;
	env_t *env_to_die;

	if ((r = envid2env(envid, &env_to_die, 1)) < 0)
		return r;
	if (env_to_die == e)
		cprintf("[%08x] exiting gracefully\n", e->env_id);
	else
		cprintf("[%08x] destroying %08x\n", e->env_id, env_to_die->env_id);
	env_destroy(env_to_die);
	return 0;
}


// TODO: Build a dispatch table instead of switching on the syscallno
// Dispatches to the correct kernel function, passing the arguments.
int32_t syscall(env_t* e, uint32_t syscallno, uint32_t a1, uint32_t a2,
                uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.

	//cprintf("Incoming syscall number: %d\n    a1: %x\n    a2: %x\n    a3: %x\n    a4: %x\n    a5: %x\n", syscallno, a1, a2, a3, a4, a5);

	assert(e); // should always have an env for every syscall
	if (INVALID_SYSCALL(syscallno))
		return -E_INVAL;

	switch (syscallno) {
		case SYS_null:
			sys_null(e);
			return 0;
		case SYS_cache_buster:
			sys_cache_buster(e, a1, a2);
			return 0;
		case SYS_cputs:
			sys_cputs(e, (char *DANGEROUS)a1, (size_t)a2);
			return 0;  // would rather have this return the number of chars put.
		case SYS_cgetc:
			return sys_cgetc(e);
		case SYS_getenvid:
			return sys_getenvid(e);
		case SYS_env_destroy:
			return sys_env_destroy(e, (envid_t)a1);
		default:
			// or just return -E_INVAL
			panic("Invalid syscall number %d for env %x!", syscallno, *e);
	}
	return 0xdeadbeef;
}

int32_t syscall_async(env_t* e, syscall_req_t *call)
{
	return syscall(e, call->num, call->args[0], call->args[1],
	               call->args[2], call->args[3], call->args[4]);
}

uint32_t process_generic_syscalls(env_t* e, uint32_t max)
{
	uint32_t count = 0;
	syscall_back_ring_t* sysbr = &e->env_sysbackring;

	// make sure the env is still alive.  TODO: this cannot handle an env being
	// freed async while this is processing.  (need a ref count or lock, etc).
	if (e->env_status == ENV_FREE)
		return 0;

	// need to switch to the right context, so we can handle the user pointer
	// that points to a data payload of the syscall
	lcr3(e->env_cr3);
	// max is the most we'll process.  max = 0 means do as many as possible
	while (RING_HAS_UNCONSUMED_REQUESTS(sysbr) && ((!max)||(count < max)) ) {
		count++;
		//printk("DEBUG PRE: sring->req_prod: %d, sring->rsp_prod: %d\n",\
			   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
		// might want to think about 0-ing this out, if we aren't
		// going to explicitly fill in all fields
		syscall_rsp_t rsp;
		// this assumes we get our answer immediately for the syscall.
		syscall_req_t* req = RING_GET_REQUEST(sysbr, ++(sysbr->req_cons));
		rsp.retval = syscall_async(e, req);
		// write response into the slot it came from
		memcpy(req, &rsp, sizeof(syscall_rsp_t));
		// update our counter for what we've produced (assumes we went in order!)
		(sysbr->rsp_prod_pvt)++;
		RING_PUSH_RESPONSES(sysbr);
		//printk("DEBUG POST: sring->req_prod: %d, sring->rsp_prod: %d\n",\
			   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
	}
	return count;
}
