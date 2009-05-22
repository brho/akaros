#include <inc/lib.h>

#ifdef __DEPUTY__
#pragma nodeputy
#endif

int main(int argc, char** argv)
{
	cprintf("goodbye, world!\n");
	cprintf("i am environment %08x\n", env->env_id);

	cprintf("about to write to shared mem.  hope it gets printed.  blimey! \n");
	async_desc_t *desc1, *desc2, *desc3;
	async_rsp_t rsp1, rsp2, rsp3;
	cprintf_async(&desc1, "Cross-Core call 1, coming from env %08x\n", env->env_id);
	cprintf("Call 1 is sent!\n");
	//cprintf_async(&desc2, "Cross-Core call 2, coming from env %08x\n", env->env_id);
	cprintf_async(&desc2, "1111111111111111111111111111111122222222222222222222222222222222333333333333333333333333333333334444444444444444444444444444444455555555555555555555555555555555666666666666666666666666666666667777777777777777777777777777777788888888888888888888888888888888Cross-Core call 2, coming from env %08x\n", env->env_id);
	cprintf("Call 2 is sent!\n");
	cprintf("Waiting on Call 1 and 2\n");
	waiton_async_call(desc1, &rsp1);
	cprintf("Received 1\n");
	waiton_async_call(desc2, &rsp2);
	cprintf_async(&desc3, "Cross-Core call 3, coming from env %08x\n", env->env_id);
	cprintf("Call 3 is sent!\n");
	waiton_async_call(desc3, &rsp3);
	// might as well spin, just to make sure nothing gets deallocated
	// while we're waiting to test the async call
	while (1);
	return 0;
}
