#include <parlib.h>
#include <vcore.h>
#include <ros/syscall.h>
#include <arc.h>

int sys_cputs_async(const char *s, size_t len, syscall_desc_t* desc,                                              
                     void (*cleanup_handler)(void*), void* cleanup_data)
{                                                                                                                     
    // could just hardcode 4 0's, will eventually wrap this marshaller anyway                                         
    syscall_req_t syscall = {SYS_cputs, 0, {(uint32_t)s, len, [2 ... (NUM_SYSCALL_ARGS-1)] 0} };                          
    desc->cleanup = cleanup_handler;                                                                                  
    desc->data = cleanup_data;                                                                                        
    return async_syscall(&syscall, desc);                                                                             
}     
int main(int argc, char** argv){
	int pid = sys_getpid();
	char testme = 't';
	printf ("single thread - init arsc \n");
	init_arc();
	async_desc_t desc1;
	async_rsp_t rsp1;
	syscall_rsp_t sysrsp;

	syscall_desc_t* sysdesc = get_sys_desc (&desc1);
	// cprintf_async(&desc1, "Cross-Core call 1, coming from process %08x\n", pid);
	sys_cputs_async(&testme, 1, sysdesc, NULL, NULL);
	waiton_syscall(sysdesc, &sysrsp);
	printf ("single thread - dummy call \n");	
}
