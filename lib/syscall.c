// System call stubs.

#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/x86.h>

static inline uint32_t
syscall(int num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	uint32_t ret;

	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	// 
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	asm volatile("int %1\n"
		: "=a" (ret)
		: "i" (T_SYSCALL),
		  "a" (num),
		  "d" (a1),
		  "c" (a2),
		  "b" (a3),
		  "D" (a4),
		  "S" (a5)
		: "cc", "memory");
	
	return ret;
}

static inline error_t async_syscall(syscall_req_t *syscall)
{
	// spin til there is room for our request.  ring size is currently 64.
	while (RING_FULL(&sysfrontring))
		cpu_relax();
	// req_prod_pvt comes in as the previously produced item.  need to
	// increment to the next available spot, which is the one we'll work on.
	syscall_req_t* req = RING_GET_REQUEST(&sysfrontring, ++(sysfrontring.req_prod_pvt));
	memcpy(req, syscall, sizeof(syscall_req_t));
	// push our updates to sysfrontring.req_prod_pvt
	RING_PUSH_REQUESTS(&sysfrontring);
	//cprintf("DEBUG: sring->req_prod: %d, sring->rsp_prod: %d\n", \
	        sysfrontring.sring->req_prod, sysfrontring.sring->rsp_prod);
	return 0;
	// at some point, we need to listen for the responses.  pass back a
	// reference of some sort, probably via a parameter.
}

void sys_cputs_async(const char *s, size_t len)
{
	// could just hardcode 4 0's, will eventually wrap this marshaller anyway
	syscall_req_t syscall = {SYS_cputs, 0, {(uint32_t)s, len, [2 ... (NUM_SYS_ARGS-1)] 0} };
	async_syscall(&syscall);
}

void sys_null()
{
	syscall(SYS_null,0,0,0,0,0);
}

void
sys_cputs(const char *s, size_t len)
{
	syscall(SYS_cputs, (uint32_t) s, len, 0, 0, 0);
}

int
sys_cgetc(void)
{
	return syscall(SYS_cgetc, 0, 0, 0, 0, 0);
}

int
sys_env_destroy(envid_t envid)
{
	return syscall(SYS_env_destroy, envid, 0, 0, 0, 0);
}

envid_t
sys_getenvid(void)
{
	 return syscall(SYS_getenvid, 0, 0, 0, 0, 0);
}


