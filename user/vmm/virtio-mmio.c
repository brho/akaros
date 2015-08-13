/*
 * Virtio MMIO bindings
 *
 * Copyright (c) 2011 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdint.h>
#include <err.h>
#include <sys/mman.h>
#include <ros/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

int debug_virtio_mmio = 1;
#define DPRINTF(fmt, ...) \
	if (debug_virtio_mmio) { printf("virtio_mmio: " fmt , ## __VA_ARGS__); }


#define VIRT_MAGIC 0x74726976 /* 'virt' */
/* version is a real mess. A real mess. I don't understand it at all. Let's stick with 1, which sucks, 
 * instead of 2, which seems to be not supported right. I think.
 */
#define VIRT_VERSION 1
#define VIRT_VENDOR 0x554D4551 /* 'QEMU' */


typedef struct {
	int state; // not used yet. */
	uint64_t bar;
	uint32_t status;
	int qsel; // queue we are on.
	int pagesize;
	int page_shift;
	int device_features_word; // if this is 1, use the high 32 bits. 
	int driver_features_word;
	struct vqdev *vqdev;
} mmiostate;

static mmiostate mmio;

void register_virtio_mmio(struct vqdev *vqdev, uint64_t virtio_base)
{
	mmio.bar = virtio_base;
	mmio.vqdev = vqdev;
}

static uint32_t virtio_mmio_read(uint64_t gpa);
char *virtio_names[] = {
	[VIRTIO_MMIO_MAGIC_VALUE] "VIRTIO_MMIO_MAGIC_VALUE",
	[VIRTIO_MMIO_VERSION] "VIRTIO_MMIO_VERSION",
	[VIRTIO_MMIO_DEVICE_ID] "VIRTIO_MMIO_DEVICE_ID",
	[VIRTIO_MMIO_VENDOR_ID] "VIRTIO_MMIO_VENDOR_ID",
	[VIRTIO_MMIO_DEVICE_FEATURES] "VIRTIO_MMIO_DEVICE_FEATURES",
	[VIRTIO_MMIO_DEVICE_FEATURES_SEL] "VIRTIO_MMIO_DEVICE_FEATURES_SEL",
	[VIRTIO_MMIO_DRIVER_FEATURES] "VIRTIO_MMIO_DRIVER_FEATURES",
	[VIRTIO_MMIO_DRIVER_FEATURES_SEL] "VIRTIO_MMIO_DRIVER_FEATURES_SEL",
	[VIRTIO_MMIO_GUEST_PAGE_SIZE] "VIRTIO_MMIO_GUEST_PAGE_SIZE",
	[VIRTIO_MMIO_QUEUE_SEL] "VIRTIO_MMIO_QUEUE_SEL",
	[VIRTIO_MMIO_QUEUE_NUM_MAX] "VIRTIO_MMIO_QUEUE_NUM_MAX",
	[VIRTIO_MMIO_QUEUE_NUM] "VIRTIO_MMIO_QUEUE_NUM",
	[VIRTIO_MMIO_QUEUE_ALIGN] "VIRTIO_MMIO_QUEUE_ALIGN",
	[VIRTIO_MMIO_QUEUE_PFN] "VIRTIO_MMIO_QUEUE_PFN",
	[VIRTIO_MMIO_QUEUE_READY] "VIRTIO_MMIO_QUEUE_READY",
	[VIRTIO_MMIO_QUEUE_NOTIFY] "VIRTIO_MMIO_QUEUE_NOTIFY",
	[VIRTIO_MMIO_INTERRUPT_STATUS] "VIRTIO_MMIO_INTERRUPT_STATUS",
	[VIRTIO_MMIO_INTERRUPT_ACK] "VIRTIO_MMIO_INTERRUPT_ACK",
	[VIRTIO_MMIO_STATUS] "VIRTIO_MMIO_STATUS",
	[VIRTIO_MMIO_QUEUE_DESC_LOW] "VIRTIO_MMIO_QUEUE_DESC_LOW",
	[VIRTIO_MMIO_QUEUE_DESC_HIGH] "VIRTIO_MMIO_QUEUE_DESC_HIGH",
	[VIRTIO_MMIO_QUEUE_AVAIL_LOW] "VIRTIO_MMIO_QUEUE_AVAIL_LOW",
	[VIRTIO_MMIO_QUEUE_AVAIL_HIGH] "VIRTIO_MMIO_QUEUE_AVAIL_HIGH",
	[VIRTIO_MMIO_QUEUE_USED_LOW] "VIRTIO_MMIO_QUEUE_USED_LOW",
	[VIRTIO_MMIO_QUEUE_USED_HIGH] "VIRTIO_MMIO_QUEUE_USED_HIGH",
	[VIRTIO_MMIO_CONFIG_GENERATION] "VIRTIO_MMIO_CONFIG_GENERATION",
};

/* We're going to attempt to make mmio stateless, since the real machine is in
 * the guest kernel. From what we know so far, all IO to the mmio space is 32 bits.
 */
static uint32_t virtio_mmio_read(uint64_t gpa)
{

	unsigned int offset = gpa - mmio.bar;
	uint32_t low;
	
	DPRINTF("virtio_mmio_read offset %s 0x%x\n", virtio_names[offset],(int)offset);

	/* If no backend is present, we treat most registers as
	 * read-as-zero, except for the magic number, version and
	 * vendor ID. This is not strictly sanctioned by the virtio
	 * spec, but it allows us to provide transports with no backend
	 * plugged in which don't confuse Linux's virtio code: the
	 * probe won't complain about the bad magic number, but the
	 * device ID of zero means no backend will claim it.
	 */
	if (mmio.vqdev->numvqs == 0) {
		switch (offset) {
		case VIRTIO_MMIO_MAGIC_VALUE:
			return VIRT_MAGIC;
		case VIRTIO_MMIO_VERSION:
			return VIRT_VERSION;
		case VIRTIO_MMIO_VENDOR_ID:
			return VIRT_VENDOR;
		default:
			return 0;
		}
	}


    // WTF? Does this happen? 
    if (offset >= VIRTIO_MMIO_CONFIG) {
	    fprintf(stderr, "Whoa. Reading past mmio config space? What gives?\n");
	    return -1;
#if 0
	    offset -= VIRTIO_MMIO_CONFIG;
	    switch (size) {
	    case 1:
		    return virtio_config_readb(vdev, offset);
	    case 2:
		    return virtio_config_readw(vdev, offset);
	    case 4:
		    return virtio_config_readl(vdev, offset);
	    default:
		    abort();
	    }
#endif
    }

#if 0
    if (size != 4) {
        DPRINTF("wrong size access to register!\n");
        return 0;
    }
#endif
DPRINTF("FUCK 0x%x\n", offset);
fprintf(stderr, "FUCK2 0x%x\n", offset);
    switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
	    return VIRT_MAGIC;
    case VIRTIO_MMIO_VERSION:
	    return VIRT_VERSION;
    case VIRTIO_MMIO_DEVICE_ID:
	    return mmio.vqdev->dev;
    case VIRTIO_MMIO_VENDOR_ID:
	    return VIRT_VENDOR;
    case VIRTIO_MMIO_DEVICE_FEATURES:
	low = mmio.vqdev->device_features >> ((mmio.device_features_word) ? 32 : 0);
	DPRINTF("RETURN from 0x%x 32 bits of word %s : 0x%x \n", mmio.vqdev->device_features, 
				mmio.device_features_word ? "high" : "low", low);
	    return low;
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
	    DPRINTF("For q %d, qnum is %d\n", mmio.qsel, mmio.vqdev->vqs[mmio.qsel].qnum);
	    return mmio.vqdev->vqs[mmio.qsel].maxqnum;
    case VIRTIO_MMIO_QUEUE_PFN:
	    return mmio.vqdev->vqs[mmio.qsel].pfn;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
	    return mmio.vqdev->vqs[mmio.qsel].isr;
    case VIRTIO_MMIO_STATUS:
	    return mmio.vqdev->vqs[mmio.qsel].status;
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
    case VIRTIO_MMIO_DRIVER_FEATURES:
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
    case VIRTIO_MMIO_QUEUE_SEL:
    case VIRTIO_MMIO_QUEUE_NUM:
    case VIRTIO_MMIO_QUEUE_ALIGN:
    case VIRTIO_MMIO_QUEUE_READY:
    case VIRTIO_MMIO_INTERRUPT_ACK:
	    fprintf(stderr, "read of write-only register@%p\n", (void *)gpa);
        return 0;
    default:
	    fprintf(stderr, "bad register offset@%p\n", (void *)gpa);
        return 0;
    }
    return 0;
}

static void virtio_mmio_write(uint64_t gpa, uint32_t value)
{
	uint64_t val64;
	uint32_t low, high;
	unsigned int offset = gpa - mmio.bar;
	
	DPRINTF("virtio_mmio_write offset %s 0x%x value 0x%x\n", virtio_names[offset], (int)offset, value);

    if (offset >= VIRTIO_MMIO_CONFIG) {
	    fprintf(stderr, "Whoa. Writing past mmio config space? What gives?\n");
#if 0
        offset -= VIRTIO_MMIO_CONFIG;
        switch (size) {
        case 1:
            virtio_config_writeb(vdev, offset, value);
            break;
        case 2:
            virtio_config_writew(vdev, offset, value);
            break;
        case 4:
            virtio_config_writel(vdev, offset, value);
            break;
        default:
            abort();
        }
#endif
        return;
    }
#if 0
    if (size != 4) {
        DPRINTF("wrong size access to register!\n");
        return;
    }
#endif
    switch (offset) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        mmio.device_features_word = value;
        break;
    case VIRTIO_MMIO_DEVICE_FEATURES:
	if (mmio.device_features_word) {
	    /* changing the high word. */
	    low = mmio.vqdev->device_features;
	    high = value;
	} else {
	    /* changing the low word. */
	    high = (mmio.vqdev->device_features >> 32);
	    low = value;
	}
	mmio.vqdev->device_features = ((uint64_t)high << 32) | low;
	DPRINTF("Set VIRTIO_MMIO_DEVICE_FEATURES to %p\n", mmio.vqdev->device_features);
	break;
    case VIRTIO_MMIO_DRIVER_FEATURES:
	if (mmio.driver_features_word) {
	    /* changing the high word. */
	    low = mmio.vqdev->driver_features;
	    high = value;
	} else {
	    /* changing the low word. */
	    high = (mmio.vqdev->driver_features >> 32);
	    low = value;
	}
	mmio.vqdev->driver_features = ((uint64_t)high << 32) | low;
	DPRINTF("Set VIRTIO_MMIO_DRIVER_FEATURES to %p\n", mmio.vqdev->driver_features);
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
	    mmio.driver_features_word = value;
        break;

    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
	    mmio.pagesize = value;
	    DPRINTF("guest page size %d bytes\n", mmio.pagesize);
        break;
    case VIRTIO_MMIO_QUEUE_SEL:
	    /* don't check it here. Check it on use. Or maybe check it here. Who knows. */
	    if (value < mmio.vqdev->numvqs)
		    mmio.qsel = value;
	    else
		    mmio.qsel = -1;
	    break;
    case VIRTIO_MMIO_QUEUE_NUM:
	mmio.vqdev->vqs[mmio.qsel].qnum = value;
        break;
    case VIRTIO_MMIO_QUEUE_ALIGN:
	mmio.vqdev->vqs[mmio.qsel].qalign = value;
        break;
    case VIRTIO_MMIO_QUEUE_PFN:
	// failure of vision: they used 32 bit numbers. Geez.
	// v2 is better, we'll do v1 for now.
	mmio.vqdev->vqs[mmio.qsel].pfn = value;
		    // let's kick off the thread and see how it goes?
		    struct virtio_threadarg *va = malloc(sizeof(*va));
		    va->arg = &mmio.vqdev->vqs[mmio.qsel];

		    va->arg->virtio = vring_new_virtqueue(mmio.qsel, 
							  mmio.vqdev->vqs[mmio.qsel].qnum,
							  mmio.vqdev->vqs[mmio.qsel].qalign,
							  false, // weak_barriers
							  mmio.vqdev->vqs[mmio.qsel].pfn,
							  NULL, NULL, /* callbacks */
 							  mmio.vqdev->vqs[mmio.qsel].name);
		    fprintf(stderr, "START THE THREAD. pfn is 0x%x, virtio is %p\n", mmio.pagesize, va->arg->virtio);
		    if (pthread_create(&va->arg->thread, NULL, va->arg->f, va)) {
			    fprintf(stderr, "pth_create failed for vq %s", va->arg->name);
			    perror("pth_create");
		    }
        break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
	    if (value < mmio.vqdev->numvqs) {
		    mmio.qsel = value;
	    }
        break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
        //vdev->isr &= ~value;
        //virtio_update_irq(vdev);
        break;
    case VIRTIO_MMIO_STATUS:
        if (!(value & VIRTIO_CONFIG_S_DRIVER_OK)) {
            printf("VIRTIO_MMIO_STATUS write: NOT OK! 0x%x\n", value);
        }

	mmio.status |= value & 0xff;

        if (value & VIRTIO_CONFIG_S_DRIVER_OK) {
            printf("VIRTIO_MMIO_STATUS write: OK! 0x%x\n", value);
        }

        break;
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
	    val64 = mmio.vqdev->vqs[mmio.qsel].qdesc;
	    val64 = val64 >> 32;
	    val64 = (val64 <<32) | value;
	    mmio.vqdev->vqs[mmio.qsel].qdesc = val64;
	    DPRINTF("qdesc set low result 0xx%x\n", val64);
	    break;
	    
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
	    val64 = (uint32_t) mmio.vqdev->vqs[mmio.qsel].qdesc;
	    mmio.vqdev->vqs[mmio.qsel].qdesc = (((uint64_t) value) <<32) | val64;
	    DPRINTF("qdesc set high result 0xx%x\n", mmio.vqdev->vqs[mmio.qsel].qdesc);
	    break;
	    
/* Selected queue's Available Ring address, 64 bits in two halves */
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
	    val64 = mmio.vqdev->vqs[mmio.qsel].qavail;
	    val64 = val64 >> 32;
	    val64 = (val64 <<32) | value;
	    mmio.vqdev->vqs[mmio.qsel].qavail = val64;
	    DPRINTF("qavail set low result 0xx%x\n", val64);
	    break;
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
	    val64 = (uint32_t) mmio.vqdev->vqs[mmio.qsel].qavail;
	    mmio.vqdev->vqs[mmio.qsel].qavail = (((uint64_t) value) <<32) | val64;
	    DPRINTF("qavail set high result 0xx%x\n", mmio.vqdev->vqs[mmio.qsel].qavail);
	    break;
	    
/* Selected queue's Used Ring address, 64 bits in two halves */
    case VIRTIO_MMIO_QUEUE_USED_LOW:
	    val64 = mmio.vqdev->vqs[mmio.qsel].qused;
	    val64 = val64 >> 32;
	    val64 = (val64 <<32) | value;
	    mmio.vqdev->vqs[mmio.qsel].qused = val64;
	    DPRINTF("qused set low result 0xx%x\n", val64);
	    break;
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
	    val64 = (uint32_t) mmio.vqdev->vqs[mmio.qsel].qused;
	    mmio.vqdev->vqs[mmio.qsel].qused = (((uint64_t) value) <<32) | val64;
	    DPRINTF("qused set used result 0xx%x\n", mmio.vqdev->vqs[mmio.qsel].qused);
	    break;
	    
	// for v2. 
    case VIRTIO_MMIO_QUEUE_READY:
	    if (value) {
		    // let's kick off the thread and see how it goes?
		    struct virtio_threadarg *va = malloc(sizeof(*va));
		    va->arg = &mmio.vqdev->vqs[mmio.qsel];
		    va->arg->virtio = (void *)(va->arg->pfn * mmio.pagesize);
		    fprintf(stderr, "START THE THREAD. pfn is 0x%x, virtio is %p\n", mmio.pagesize, va->arg->virtio);
		    if (pthread_create(&va->arg->thread, NULL, va->arg->f, va)) {
			    fprintf(stderr, "pth_create failed for vq %s", va->arg->name);
			    perror("pth_create");
		    }
	    }
	    break;

    case VIRTIO_MMIO_MAGIC_VALUE:
    case VIRTIO_MMIO_VERSION:
    case VIRTIO_MMIO_DEVICE_ID:
    case VIRTIO_MMIO_VENDOR_ID:
//    case VIRTIO_MMIO_HOSTFEATURES:
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        DPRINTF("write to readonly register\n");
        break;

    default:
        DPRINTF("bad register offset 0x%x\n", offset);
    }

}

static char *modrmreg[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};

void virtio_mmio(struct vmctl *v)
{
	int advance = 3; /* how much to move the IP forward at the end. 3 is a good default. */
	// All virtio accesses seem to be 32 bits.
	// valp points to a place to get or put the value. 
	uint32_t *valp;
	//DPRINTF("v is %p\n", v);
	// regp points to the register in hw_trapframe from which
	// to load or store a result.
	uint64_t *regp;

	// Duh, which way did he go George? Which way did he go? 
	// First hit on Google gets you there!
	// This is the guest physical address of the access.
	// This is nice, because if we ever go with more complete
	// instruction decode, knowing this gpa reduces our work:
	// we don't have to find the source address in registers,
	// only the register holding or receiving the value.
	uint64_t gpa = v->gpa;
	//DPRINTF("gpa is %p\n", gpa);

	// To find out what to do, we have to look at
	// RIP. Technically, we should read RIP, walk the page tables
	// to find the PA, and read that. But we're in the kernel, so
	// we take a shortcut for now: read the low 30 bits and use
	// that as the kernel PA, or our VA, and see what's
	// there. Hokey. Works.
	uint8_t *kva = (void *)(v->regs.tf_rip & 0x3fffffff);
	//DPRINTF("kva is %p\n", kva);

	if ((kva[0] != 0x8b) && (kva[0] != 0x89)) {
		fprintf(stderr, "%s: can't handle instruction 0x%x\n", kva[0]);
		return;
	}

	uint16_t ins = *(uint16_t *)kva;
	//DPRINTF("ins is %04x\n", ins);
	
	int write = (kva[0] == 0x8b) ? 0 : 1;
	if (write)
		valp = (uint32_t *)gpa;

	int mod = kva[1]>>6;
	switch (mod) {
		case 0: 
		case 3:
			advance = 2;
			break;
		case 1:
			advance = 3;
			break;
		case 2: 
			advance = 6;
			break;
	}
	/* the dreaded mod/rm byte. */
	int destreg = (ins>>11) & 7;
	// Our primitive approach wins big here.
	// We don't have to decode the register or the offset used
	// in the computation; that was done by the CPU and is the gpa.
	// All we need to know is which destination or source register it is.
	switch (destreg) {
	case 0:
		regp = &v->regs.tf_rax;
		break;
	case 1:
		regp = &v->regs.tf_rcx;
		break;
	case 2:
		regp = &v->regs.tf_rdx;
		break;
	case 3:
		regp = &v->regs.tf_rbx;
		break;
	case 4:
		regp = &v->regs.tf_rsp; // uh, right.
		break;
	case 5:
		regp = &v->regs.tf_rbp;
		break;
	case 6:
		regp = &v->regs.tf_rsi;
		break;
	case 7:
		regp = &v->regs.tf_rdi;
		break;
	}

	if (write) {
		virtio_mmio_write(gpa, *regp);
		DPRINTF("Write: mov %s to %s @%p val %p\n", modrmreg[destreg], virtio_names[(uint8_t)gpa], gpa, *regp);
	} else {
		*regp = virtio_mmio_read(gpa);
		DPRINTF("Read: Set %s from %s @%p to %p\n", modrmreg[destreg], virtio_names[(uint8_t)gpa], gpa, *regp);
	}

	DPRINTF("Advance rip by %d bytes to %p\n", advance, v->regs.tf_rip);
	v->regs.tf_rip += advance;
}
