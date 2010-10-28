#include <parlib.h>
#include <vcore.h>
#include <ros/syscall.h>
#include <arc.h>
#include <stdio.h>

syscall_desc_t* sys_cputs_async(const char *s, size_t len,                                             
                     void (*cleanup_handler)(void*), void* cleanup_data)
{                                                                                                                     
    // could just hardcode 4 0's, will eventually wrap this marshaller anyway                                         
	syscall_desc_t* desc;
    syscall_req_t syscall = {REQ_alloc, cleanup_handler, cleanup_data,
							SYS_cputs,{(uint32_t)s, len, [2 ... (NUM_SYSCALL_ARGS-1)] 0} };                          
    syscall.cleanup = cleanup_handler;                                                                                  
    syscall.data = cleanup_data;                                                                                        
    async_syscall(&syscall, &desc);
	return desc;
}

int main(int argc, char** argv){
	int pid = sys_getpid();
	char testme = 't';
	printf ("single thread - init arsc \n");
	syscall_desc_t* sysdesc;
	syscall_rsp_t sysrsp;
	init_arc(&SYS_CHANNEL);

	printf ("single thread - init complete \n");
	// cprintf_async(&desc1, "Cross-Core call 1, coming from process %08x\n", pid);
	sysdesc = sys_cputs_async(&testme, 1, NULL, NULL);

	printf ("single thread - call placed \n");
	waiton_syscall(sysdesc, &sysrsp);	
	printf ("single thread - dummy call \n");	
}
