#include <ros/syscall.h>
#include <parlib/arch/vcore.h>

struct syscall vc_entry = {
	.num = SYS_vc_entry,
	.err = 0,
	.retval = 0,
	.flags = 0,
	.ev_q = 0,
	.u_data = 0,
	.arg0 = 0,
	.arg1 = 0,
	.arg2 = 0,
	.arg3 = 0,
	.arg4 = 0,
	.arg5 = 0,
};
