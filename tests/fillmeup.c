#include <stdio.h>
#include <stdint.h>
#include <ros/syscall.h>
#include <vcore.h>
#include <assert.h>
#include <parlib.h>

#define BUF_SIZE 1024
#define NUM_BUFS 1024
uint8_t bufs[BUF_SIZE * NUM_BUFS];
int16_t last_written;

void *mytls_desc = NULL;

void print_new_buf() 
{
	printf("I just woke up on my vcore!\n");
	printf("Value of last_written: %d\n", last_written);
//	printf("Contents of last written buffer:\n");
//	for(int i=0; i<BUF_SIZE; i++)
//		printf("0x%02x ", bufs[BUF_SIZE * last_written + i]);
}

void vcore_entry()
{
	uint32_t vcoreid = vcore_id();

	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	struct vcore *vc = &__procinfo.vcoremap[vcoreid];

	/* Should always have notifications disabled when coming in here. */
	assert(vcpd->notif_enabled == FALSE);

	/* Put this in the loop that deals with notifications.  It will return if
	 * there is no preempt pending. */ 
	// TODO: prob make a handle_notif() function
	if (vc->preempt_pending)
		sys_yield(TRUE);
	vcpd->notif_pending = 0;

	if(last_written >= 0)
		print_new_buf();
		
	/* Pop the user trap frame */
	set_tls_desc(mytls_desc, vcoreid);
	pop_ros_tf(&vcpd->notif_tf, vcoreid);
	assert(0);
}

int main(int argc, char** argv)
{
/* begin: stuff userspace needs to do before switching to multi-mode */
	if (vcore_init())
		printf("vcore_init() failed, we're fucked!\n");

	/* tell the kernel where and how we want to receive notifications */
	struct notif_method *nm;
	nm = &__procdata.notif_methods[NE_NONE];
	nm->flags |= NOTIF_WANTED | NOTIF_IPI;
	nm->vcoreid = 0;
	nm = &__procdata.notif_methods[NE_ETC_ETC_ETC];
	nm->flags |= NOTIF_WANTED | NOTIF_IPI;
	nm->vcoreid = 0;

	mytls_desc = get_tls_desc(0);

	/* don't forget to enable notifs on vcore0 at some point */
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;
	
/* end: stuff userspace needs to do before switching to multi-mode */

	printf("About to fill me up!!\n");
	ros_syscall(SYS_fillmeup, bufs, NUM_BUFS, &last_written, 0, 0);
	vcore_request(1);
	while(1) {
		sys_halt_core(0);
	}
	return 0;
}


