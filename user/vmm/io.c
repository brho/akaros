#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <vmm/coreboot_tables.h>
#include <ros/common.h>
#include <vmm/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/sched.h>
#include <ros/arch/trapframe.h>

/* crude PCI bus. Just enough to get virtio working. I would rather not add to this. */
struct pciconfig {
	uint32_t registers[256];
};

/* just index by devfn, i.e. 8 bits */
struct pciconfig pcibus[] = {
	/* linux requires that devfn 0 be a bridge.
	 * 00:00.0 Host bridge: Intel Corporation 440BX/ZX/DX - 82443BX/ZX/DX Host bridge (rev 01)
	 */
	{
		{0x71908086, 0x02000006, 0x06000001},
	},
};
/* cf8 is a single-threaded resource. */
static uint32_t cf8;
static uint32_t allones = (uint32_t)-1;

/* Return a pointer to the 32-bit "register" in the "pcibus" give an address.
 * Use cf8.  only for readonly access.  this will fail if we ever want to do
 * writes, but we don't.
 */
void regp(uint32_t **reg)
{
	*reg = &allones;
	int devfn = (cf8>>8) & 0xff;
	//printf("devfn %d\n", devfn);
	if (devfn < COUNT_OF(pcibus))
		*reg = &pcibus[devfn].registers[(cf8>>2)&0x3f];
	//printf("-->regp *reg 0x%lx\n", **reg);
}

static void configaddr(uint32_t val)
{
	printd("%s 0x%lx\n", __func__, val);
	cf8 = val;
}

static void configread32(uint32_t edx, uint64_t *reg)
{
	uint32_t *r = &cf8;
	regp(&r);
	*reg = *r;
	printd("%s: 0x%lx 0x%lx, 0x%lx 0x%lx\n", __func__, cf8, edx, r, *reg);
}

static void configread16(uint32_t edx, uint64_t *reg)
{
	uint64_t val;
	int which = ((edx&2)>>1) * 16;
	configread32(edx, &val);
	val >>= which;
	*reg = val;
	printd("%s: 0x%lx, 0x%lx 0x%lx\n", __func__, edx, val, *reg);
}

static void configread8(uint32_t edx, uint64_t *reg)
{
	uint64_t val;
	int which = (edx&3) * 8;
	configread32(edx, &val);
	val >>= which;
	*reg = val;
	printd("%s: 0x%lx, 0x%lx 0x%lx\n", __func__, edx, val, *reg);
}

static void configwrite32(uint32_t addr, uint32_t val)
{
	uint32_t *r = &cf8;
	regp(&r);
	*r = val;
	printd("%s 0x%lx 0x%lx\n", __func__, addr, val);
}

static void configwrite16(uint32_t addr, uint16_t val)
{
	printd("%s 0x%lx 0x%lx\n", __func__, addr, val);
}

static void configwrite8(uint32_t addr, uint8_t val)
{
	printd("%s 0x%lx 0x%lx\n", __func__, addr, val);
}

/* this is very minimal. It needs to move to vmm/io.c but we don't
 * know if this minimal approach will even be workable. It only (for
 * now) handles pci config space. We'd like to hope that's all we will
 * need.
 * It would have been nice had intel encoded the IO exit info as nicely as they
 * encoded, some of the other exits.
 */
int io(struct guest_thread *vm_thread)
{

	/* Get a pointer to the memory at %rip. This is quite messy and part of
	 * the reason we don't want to do this at all. It sucks. Would have been
	 * nice had linux had an option to ONLY do mmio config space access, but
	 * no such luck.  */
	uint8_t *ip8 = NULL;
	uint16_t *ip16;
	uintptr_t ip;
	uint32_t edx;
	uint32_t eax;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	/* Get the RIP of the io access. */
	if (rippa(vm_thread, (uint64_t *)&ip))
		return VM_PAGE_FAULT;
	edx = vm_tf->tf_rdx;
	eax = vm_tf->tf_rax;
	ip8 = (void *)ip;
	ip16 = (void *)ip;
	//printf("io: ip16 %p\n", *ip16, edx);

	if (*ip8 == 0xef) {
		vm_tf->tf_rip += 1;
		/* out at %edx */
		if (edx == 0xcf8) {
			//printf("Set cf8 ");
			configaddr(vm_tf->tf_rax);
			return 0;
		}
		if (edx == 0xcfc) {
			//printf("Set cfc ");
			configwrite32(edx, vm_tf->tf_rax);
			return 0;
		}
		/* While it is perfectly legal to do IO operations to
		 * nonexistant places, we print a warning here as it
		 * might also indicate a problem.  In practice these
		 * types of IOs happens less frequently, and whether
		 * they are bad or not is not always easy to decide.
		 * Simple example: for about the first 10 years Linux
		 * used to outb 0x98 to port 0x80 while idle. We
		 * wouldn't want to call that an error, but that kind
		 * of thing is a bad practice we ought to know about,
		 * because it can cause chipset errors and result in
		 * other non-obvious failures (in one case, breaking
		 * BIOS reflash operations).  Plus, true story, it
		 * confused people into thinking we were running
		 * Windows 98, not Linux.
		 */
		printf("(out rax, edx): unhandled IO address dx @%p is 0x%x\n",
		       ip8, edx);
		return 0;
	}
	/* TODO: sort out these various OUT operations */
	// out %al, %dx
	if (*ip8 == 0xee) {
		vm_tf->tf_rip += 1;
		/* out al %edx */
		if (edx == 0xcfb) { // special!
			printf("Just ignore the damned cfb write\n");
			return 0;
		}
		if ((edx&~3) == 0xcfc) {
			//printf("ignoring write to cfc ");
			return 0;
		}
		if (edx == 0xcf9) {
			// on real hardware, an outb to 0xcf9 with bit 2 set is
			// about as hard a reset as you can get. It yanks the
			// reset on everything, including all the cores.  It
			// usually happens after the kernel has done lots of
			// work to carefully quiesce the machine but, once it
			// happens, game is over. Hence, an exit(0) is most
			// appropriate, since it's not an error.
			if (eax & (1 << 2)) {
				printf("outb to PCI reset port with bit 2 set: time to die\n");
				exit(0);
			}
			return 0;
		}

		/* Another case where we print a message but it's not an error.
		 * */
		printf("out al, dx: unhandled IO address dx @%p is 0x%x\n", ip8,
		       edx); return 0;
	}
	/* Silently accept OUT imm8, al */
	if (*ip8 == 0xe6) {
		vm_tf->tf_rip += 2;
		return 0;
	}
	/* Silently accept OUT dx, ax with opcode size modifier */
	if (*ip16 ==  0xef66) {
		vm_tf->tf_rip += 2;
		return 0;
	}
	if (*ip8 == 0xec) {
		vm_tf->tf_rip += 1;
		//printf("configread8 ");
		configread8(edx, &vm_tf->tf_rax);
		return 0;
	}
	if (*ip8 == 0xed) {
		vm_tf->tf_rip += 1;
		if (edx == 0xcf8) {
			//printf("read cf8 0x%lx\n", v->regs.tf_rax);
			vm_tf->tf_rax = cf8;
			return 0;
		}
		//printf("configread32 ");
		configread32(edx, &vm_tf->tf_rax);
		return 0;
	}
	/* Detects when something is read from the PIC, so
	 * a value signifying there is no PIC is given.
	 */
	if (*ip16 == 0x21e4) {
		vm_tf->tf_rip += 2;
		vm_tf->tf_rax |= 0x00000ff;
		return 0;
	}
	if (*ip16 == 0xed66) {
		vm_tf->tf_rip += 2;
		//printf("configread16 ");
		configread16(edx, &vm_tf->tf_rax);
		return 0;
	}

	/* This is, so far, the only case in which we indicate
	 * failure: we can't even decode the instruction. We've
	 * implemented the common cases above, and recently this
	 * failure has been seen only when the RIP is set to some
	 * bizarre value and we start fetching instructions from
	 * (e.g.) the middle of a page table. PTEs look like IO
	 * instructions to the CPU.
	 */
	printf("unknown IO %p %x %x\n", ip8, *ip8, *ip16);
	return -1;
}

