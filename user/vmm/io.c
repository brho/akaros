#include <stdio.h> 
#include <pthread.h>
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

/* nowhere on my linux system. */
#define ARRAY_SIZE(x) (sizeof((x))/sizeof((x)[0]))

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

/* Return a pointer to the 32-bit "register" in the "pcibus" give an address. Use cf8.
 * only for readonly access.
 * this will fail if we ever want to do writes, but we don't.
 */
void regp(uint32_t **reg)
{
	*reg = &allones;
	int devfn = (cf8>>8) & 0xff;
	//printf("devfn %d\n", devfn);
	if (devfn < ARRAY_SIZE(pcibus))
		*reg = &pcibus[devfn].registers[(cf8>>2)&0x3f];
	//printf("-->regp *reg 0x%lx\n", **reg);
}

static uint32_t configaddr(uint32_t val)
{
	printf("%s 0x%lx\n", __func__, val);
	cf8 = val;
	return 0;
}

static uint32_t configread32(uint32_t edx, uint64_t *reg)
{
	uint32_t *r = &cf8;
	regp(&r);
	*reg = *r;
	printf("%s: 0x%lx 0x%lx, 0x%lx 0x%lx\n", __func__, cf8, edx, r, *reg);
	return 0;
}

static uint32_t configread16(uint32_t edx, uint64_t *reg)
{
	uint64_t val;
	int which = ((edx&2)>>1) * 16;
	configread32(edx, &val);
	val >>= which;
	*reg = val;
	printf("%s: 0x%lx, 0x%lx 0x%lx\n", __func__, edx, val, *reg);
	return 0;
}

static uint32_t configread8(uint32_t edx, uint64_t *reg)
{
	uint64_t val;
	int which = (edx&3) * 8;
	configread32(edx, &val);
	val >>= which;
	*reg = val;
	printf("%s: 0x%lx, 0x%lx 0x%lx\n", __func__, edx, val, *reg);
	return 0;
}

static int configwrite32(uint32_t addr, uint32_t val)
{
	uint32_t *r = &cf8;
	regp(&r);
	*r = val;
	printf("%s 0x%lx 0x%lx\n", __func__, addr, val);
	return 0;
}

static int configwrite16(uint32_t addr, uint16_t val)
{
	printf("%s 0x%lx 0x%lx\n", __func__, addr, val);
	return 0;
}

static int configwrite8(uint32_t addr, uint8_t val)
{
	printf("%s 0x%lx 0x%lx\n", __func__, addr, val);
	return 0;
}

/* this is very minimal. It needs to move to vmm/io.c but we don't
 * know if this minimal approach will even be workable. It only (for
 * now) handles pci config space. We'd like to hope that's all we will
 * need.
 * It would have been nice had intel encoded the IO exit info as nicely as they
 * encoded, some of the other exits.
 */
int io(struct vmctl *v)
{

	/* Get a pointer to the memory at %rip. This is quite messy and part of the
	 * reason we don't want to do this at all. It sucks. Would have been nice
	 * had linux had an option to ONLY do mmio config space access, but no such
	 * luck.
	 */
	uint8_t *ip8 = NULL;
	uint16_t *ip16;
	uintptr_t ip;
	uint32_t edx;
	/* for now, we're going to be a bit crude. In kernel, p is about v, so we just blow away
	 * the upper 34 bits and take the rest + 1M as our address
	 * TODO: put this in vmctl somewhere?
	 */
	ip = v->regs.tf_rip & 0x3fffffff;
	edx = v->regs.tf_rdx;
	ip8 = (void *)ip;
	ip16 = (void *)ip;
	//printf("io: ip16 %p\n", *ip16, edx);

	if (*ip8 == 0xef) {
		v->regs.tf_rip += 1;
		/* out at %edx */
		if (edx == 0xcf8) {
			//printf("Set cf8 ");
			return configaddr(v->regs.tf_rax);
		}
		if (edx == 0xcfc) {
			//printf("Set cfc ");
			return configwrite32(edx, v->regs.tf_rax);
		}
		printf("(out rax, edx): unhandled IO address dx @%p is 0x%x\n", ip8, edx);
		return -1;
	}
	// out %al, %dx
	if (*ip8 == 0xee) {
		v->regs.tf_rip += 1;
		/* out al %edx */
		if (edx == 0xcfb) { // special!
			printf("Just ignore the damned cfb write\n");
			return 0;
		}
		if ((edx&~3) == 0xcfc) {
			//printf("ignoring write to cfc ");
			return 0;
		}
		printf("out al, dx: unhandled IO address dx @%p is 0x%x\n", ip8, edx);
		return -1;
	}
	if (*ip8 == 0xec) {
		v->regs.tf_rip += 1;
		//printf("configread8 ");
		return configread8(edx, &v->regs.tf_rax);
	}
	if (*ip8 == 0xed) {
		v->regs.tf_rip += 1;
		if (edx == 0xcf8) {
			//printf("read cf8 0x%lx\n", v->regs.tf_rax);
			v->regs.tf_rax = cf8;
			return 0;
		}
		//printf("configread32 ");
		return configread32(edx, &v->regs.tf_rax);
	}
	if (*ip16 == 0xed66) {
		v->regs.tf_rip += 2;
		//printf("configread16 ");
		return configread16(edx, &v->regs.tf_rax);
	}
	printf("unknown IO %p %x %x\n", ip8, *ip8, *ip16);
	return -1;
}

