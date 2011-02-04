#ifdef __CONFIG_ARSC_SERVER__
#include <parlib.h>
#include <vcore.h>
#include <ros/syscall.h>
#include <arc.h>
#include <stdio.h>

syscall_desc_t* sys_cputs_async(const char *s, size_t len,                                             
                     void (*cleanup_handler)(void*), void* cleanup_data)
{                                                                                                                     
    /*// could just hardcode 4 0's, will eventually wrap this marshaller anyway                                         
	syscall_desc_t* desc;
    syscall_req_t syscall = {REQ_alloc, cleanup_handler, cleanup_data,
							SYS_cputs,{(uint32_t)s, len, [2 ... (NUM_SYSCALL_ARGS-1)] 0} };                          
    syscall.cleanup = cleanup_handler;                                                                                  
    syscall.data = cleanup_data;
    async_syscall(&syscall, &desc);
	*/
	return arc_call(SYS_cputs, s, len);
}

int main(int argc, char** argv){
	int pid = sys_getpid();
	char testme = 't';
	printf ("single thread - init arsc \n");
	syscall_desc_t* sysdesc[2];
	syscall_rsp_t sysrsp;
	init_arc(&SYS_CHANNEL);

	printf ("single thread - init complete \n");
	// cprintf_async(&desc1, "Cross-Core call 1, coming from process %08x\n", pid);
	sysdesc[0] = sys_cputs_async(&testme, 1, NULL, NULL);
	sysdesc[1] = sys_cputs_async(&testme, 1, NULL, NULL);

	printf ("single thread - call placed \n");
	//ignore return value
	assert(-1 != waiton_syscall(sysdesc[0]));
	assert(-1 != waiton_syscall(sysdesc[1]));
	printf ("single thread - dummy call \n");	
}

#else
int main(){};
#endif
