#include <ros/resource.h>
#include <ros/procdata.h>
#include <ros/event.h>
#include <ros/bcq.h>
#include <parlib.h>
#include <vcore.h>

#include <stdio.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <timing.h>
#include <assert.h>
#include <event.h>
#include <uthread.h>

void *core0_tls = 0;
void *in_buf, *out_buf;

/* Test program for the audio device.  mmap()s the stuff, sets up a notif
 * handler, and switches to multi_mode.
 *
 * Note: this has a lot of mhello-like MCP infrastructure.  When Lithe is
 * working, you won't need any of this.  Just the mmap stuff and the notif
 * handler.  Stuff specific to the ethernet audio device is marked ETH_AUD. */
int main() 
{ 
	int retval;
	int in_fd, out_fd;
	/* ETHAUD mmap the input and output buffers */
	in_fd = open("/dev/eth_audio_in", O_RDONLY);
	out_fd = open("/dev/eth_audio_out", O_RDWR);
	assert(in_fd != -1);
	assert(out_fd != -1);
	in_buf = mmap(0, PGSIZE, PROT_READ, 0, in_fd, 0);
	if (in_buf == MAP_FAILED) {
		int err = errno;
		perror("Can't mmap the input buf:");
	}
	out_buf = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_POPULATE, out_fd, 0);
	if (out_buf == MAP_FAILED) {
		int err = errno;
		perror("Can't mmap the output buf:");
	}
	//strncpy(out_buf, "Nanwan loves you!\n", 19);

/* begin: stuff userspace needs to do before switching to multi-mode */
	if (vcore_init())
		printf("vcore_init() failed, we're fucked!\n");

	/* ETHAUD Turn on Free apple pie (which is the network packet) */
	enable_kevent(EV_FREE_APPLE_PIE, 0, EVENT_IPI);

	/* Need to save this somewhere that you can find it again when restarting
	 * core0 */
	core0_tls = get_tls_desc(0);
	/* Need to save our floating point state somewhere (like in the
	 * user_thread_tcb so it can be restarted too */

	/* don't forget to enable notifs on vcore0 at some point */
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;
	
/* end: stuff userspace needs to do before switching to multi-mode */
	/* ETHAUD */
	/* Switch into _M mode */
	retval = vcore_request(1);

	/* Stay alive for a minute (will take interrupts) */
	udelay(60000000);

	printf("Eth aud, finishing up!\n");
	/* Need to do unmap it, due to some limitations in the kernel */
	munmap(in_buf, PGSIZE);
	munmap(out_buf, PGSIZE);
	close(in_fd);
	close(out_fd);
}

/* ETHAUD, with some debugging commands... */
void process_packet(void)
{
	//printf("Received a packet!\n");
	//memset(out_buf, 0, PGSIZE);
	memcpy(out_buf, in_buf, 1280);	/* size of the payload. */
	memset(out_buf + 1280, 0, 4);	/* 4 bytes of control info. */
	//printf("contents of out_buf %s\n", out_buf);
}

void vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();
	static bool first_time = TRUE;

	printf("GIANT WARNING: this is ancient shit\n");
/* begin: stuff userspace needs to do to handle events/notifications */

	struct vcore *vc = &__procinfo.vcoremap[vcoreid];
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[vcoreid];
	
	/* Ghetto way to get just an event number */
	unsigned int ev_type = get_event_type(&vcpd->ev_mbox);

	/* ETHAUD app: process the packet if we got a notif */
	if (ev_type == EV_FREE_APPLE_PIE)
		process_packet();

	if (vc->preempt_pending) {
		printf("Oh crap, vcore %d is being preempted!  Yielding\n", vcoreid);
		sys_yield(TRUE);
		printf("After yield on vcore %d. I wasn't being preempted.\n", vcoreid);
	}
		
	/* Lets try to restart vcore0's context.  Note this doesn't do anything to
	 * set the appropriate TLS.  On x86, this will involve changing the LDT
	 * entry for this vcore to point to the TCB of the new user-thread. */
	if (vcoreid == 0) {
		clear_notif_pending(vcoreid);
		set_tls_desc(core0_tls, 0);
		/* Load silly state (Floating point) too */
		pop_ros_tf(&vcpd->notif_tf, vcoreid);
		printf("should never see me!");
	}	
	/* unmask notifications once you can let go of the notif_tf and it is okay
	 * to clobber the transition stack.
	 * Check Documentation/processes.txt: 4.2.4.  In real code, you should be
	 * popping the tf of whatever user process you want (get off the x-stack) */
	vcpd->notif_enabled = TRUE;
	
/* end: stuff userspace needs to do to handle notifications */
	/* The other vcores will hit here. */
	while (1)
		cpu_relax();
}
