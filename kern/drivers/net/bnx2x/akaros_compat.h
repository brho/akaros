/* Copyright (c) 2015 Google Inc.
 *
 * Dumping ground for converting between Akaros and other OSs. */

#ifndef ROS_KERN_AKAROS_COMPAT_H
#define ROS_KERN_AKAROS_COMPAT_H

/* Common headers that most driver files will need */

#include <assert.h>
#include <error.h>
#include <ip.h>
#include <kmalloc.h>
#include <kref.h>
#include <pmap.h>
#include <slab.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <bitmap.h>
#include <mii.h>
#include <umem.h>
#include <mmio.h>
#include <taskqueue.h>

#define __rcu
#define unlikely(x) (x)

typedef unsigned long dma_addr_t;
/* these dma funcs are empty in linux with !CONFIG_NEED_DMA_MAP_STATE */
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           (0)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             (0)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)
typedef int pci_power_t;

#define DEFINE_SEMAPHORE(name)  \
    struct semaphore name = SEMAPHORE_INITIALIZER_IRQSAVE(name, 1)
#define sema_init(sem, val) sem_init_irqsave(sem, val)
#define up(sem) sem_up(sem)
#define down(sem) sem_down(sem)
#define down_trylock(sem) ({!sem_trydown(sem);})
/* In lieu of spatching, I wanted to keep the distinction between down and
 * down_interruptible/down_timeout.  Akaros doesn't have the latter. */
#define down_interruptible(sem) ({sem_down(sem); 0;})
#define down_timeout(sem, timeout) ({sem_down(sem); 0;})


/* I'd like to spatch all of the pci methods, but I don't know how to do the
 * reads.  Since we're not doing the reads, then no sense doing the writes. */
static inline int pci_read_config_byte(struct pci_device *dev, uint32_t off,
                                       uint8_t *val)
{
	*val = pcidev_read8(dev, off);
	return 0;
}
                                       
static inline int pci_read_config_word(struct pci_device *dev, uint32_t off,
                                       uint16_t *val)
{
	*val = pcidev_read16(dev, off);
	return 0;
}
                                       
static inline int pci_read_config_dword(struct pci_device *dev, uint32_t off,
                                        uint32_t *val)
{
	*val = pcidev_read32(dev, off);
	return 0;
}
                                       
static inline int pci_write_config_byte(struct pci_device *dev, uint32_t off,
                                        uint8_t val)
{
	pcidev_write8(dev, off, val);
	return 0;
}

static inline int pci_write_config_word(struct pci_device *dev, uint32_t off,
                                        uint16_t val)
{
	pcidev_write16(dev, off, val);
	return 0;
}

static inline int pci_write_config_dword(struct pci_device *dev, uint32_t off,
                                         uint32_t val)
{
	pcidev_write32(dev, off, val);
	return 0;
}

#endif /* ROS_KERN_AKAROS_COMPAT_H */
