/* Various parts of FreeBSD x86/include/bus.h
 *
 * Copyright (c) KATO Takenori, 1999.
 *
 * All rights reserved.  Unpublished rights reserved under the copyright
 * laws of Japan.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/x86/include/bus.h 263289 2014-03-18 01:40:25Z emaste $
 */

/*      $NetBSD: bus.h,v 1.12 1997/10/01 08:25:15 fvdl Exp $    */

/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *      for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Modified for Akaros */

#ifndef ROS_BSD_BUS_H
#define ROS_BSD_BUS_H

typedef uintptr_t bus_addr_t;
typedef uintptr_t bus_size_t;
typedef uintptr_t bus_space_handle_t;
typedef void *bus_dma_tag_t;
typedef void *bus_dmamap_t;
typedef uintptr_t bus_dma_segment_t;
typedef uintptr_t bus_space_tag_t;

/* ret is the location of the tag we create, which is just the size */
#define bus_get_dma_tag(pcidev) (0)
#define bus_dma_tag_create(_tag, _align, _bound, _low, _hi, _fil, _filarg,     \
                           _size, _num, _size2, _flag2, _lock, _lockarg, _ret) \
({                                                                             \
	*(size_t*)(_ret) = (size_t)(_size);                                        \
	0;                                                                         \
})
#define bus_dma_tag_destroy(...)
#define bus_dmamem_alloc(_tag, _vaddraddr, _flags, _map)                       \
({                                                                             \
	size_t order = LOG2_UP(nr_pages((size_t)(_tag)));                          \
	int ret = -ENOMEM;                                                         \
	void *vaddr = get_cont_pages(order, 0);                                    \
	if (vaddr) {                                                               \
		memset(vaddr, 0, (size_t)(_tag));                                      \
		ret = 0;                                                               \
		*(_vaddraddr) = vaddr;                                                 \
	}                                                                          \
	ret;                                                                       \
})
#define bus_dmamem_free(_tag, _vaddr, _map)                                    \
({                                                                             \
	size_t order = LOG2_UP(nr_pages((size_t)(_tag)));                          \
	free_cont_pages((_vaddr), order);                                          \
})
#define bus_dmamap_sync(...)
/* bxe_dma_map_addr is actually a callback tht does the paddr assignment */
#define bus_dmamap_load(_tag, _map, _vaddr, _size, _map_addr, _dma, _flag)     \
({                                                                             \
	(_dma)->paddr = PADDR((_dma)->vaddr);                                      \
	0;                                                                         \
})
#define bus_dmamap_unload(...)
#define bus_dmamap_create(...) (0)
#define bus_dmamap_destroy(...)

/* We should be fine with irq = 0.  When we use BusPCI, it is ignored.  We
 * might need to deal with the filter too. */
#define bus_setup_intr(pci, res, flags, filter, handler, arg, tag)             \
	register_irq(0, (handler), (arg),                                          \
                 MKBUS(BusPCI, (pci)->bus, (pci)->dev, (pci)->func))
#define bus_teardown_intr(...)
#define bus_describe_intr(...)
#define bus_bind_intr(...)

/* This is supposed to be an mbuf chain.  Just give them a blob for now */
#define m_getjcl(ign1, ign2, ign3, _size) kzmalloc((_size), KMALLOC_WAIT)
#define m_freem(m) kfree(m)
#define m_free(m) kfree(m)
#define bus_dmamap_load_mbuf_sg(_tag, _map, _mbuf, _segs, _nsegs, _flag)       \
({                                                                             \
	(_segs)[0] = PADDR((_mbuf));                                               \
	*(_nsegs) = 1;                                                             \
	0;                                                                         \
})

/* Bus read/write barrier methods.
 *
 *      void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t bsh,
 *                             bus_size_t offset, bus_size_t len, int flags);
 *
 *
 * Note that BUS_SPACE_BARRIER_WRITE doesn't do anything other than
 * prevent reordering by the compiler; all Intel x86 processors currently
 * retire operations outside the CPU in program order.
 */
#define BUS_SPACE_BARRIER_READ  0x01	/* force read barrier */
#define BUS_SPACE_BARRIER_WRITE 0x02	/* force write barrier */

static inline void bus_space_barrier(bus_space_tag_t tag,
									 bus_space_handle_t bsh, bus_size_t offset,
									 bus_size_t len, int flags)
{
	if (flags & BUS_SPACE_BARRIER_READ)
		bus_rmb();
	else
		bus_wmb();
}

/*
 * Values for the x86 bus space tag, not to be used directly by MI code.
 */
#define X86_BUS_SPACE_IO        0	/* space is i/o space */
#define X86_BUS_SPACE_MEM       1	/* space is mem space */

#define BUS_SPACE_MAXSIZE_24BIT 0xFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT 0xFFFFFFFF
#define BUS_SPACE_MAXSIZE       0xFFFFFFFF
#define BUS_SPACE_MAXADDR_24BIT 0xFFFFFF
#define BUS_SPACE_MAXADDR_32BIT 0xFFFFFFFF
#if defined(__amd64__) || defined(PAE)
#define BUS_SPACE_MAXADDR       0xFFFFFFFFFFFFFFFFULL
#else
#define BUS_SPACE_MAXADDR       0xFFFFFFFF
#endif

#define BUS_SPACE_INVALID_DATA  (~0)
#define BUS_SPACE_UNRESTRICTED  (~0)

static inline uint8_t bus_space_read_1(bus_space_tag_t tag,
									   bus_space_handle_t handle,
									   bus_size_t offset)
{
	if (tag == X86_BUS_SPACE_IO)
		return (inb(handle + offset));
	return (*(volatile uint8_t *)(handle + offset));
}

static inline uint16_t bus_space_read_2(bus_space_tag_t tag,
										bus_space_handle_t handle,
										bus_size_t offset)
{
	if (tag == X86_BUS_SPACE_IO)
		return (inw(handle + offset));
	return (*(volatile uint16_t *)(handle + offset));
}

static inline uint32_t bus_space_read_4(bus_space_tag_t tag,
										bus_space_handle_t handle,
										bus_size_t offset)
{
	if (tag == X86_BUS_SPACE_IO)
		return (inl(handle + offset));
	return (*(volatile uint32_t *)(handle + offset));
}

static inline void bus_space_write_1(bus_space_tag_t tag,
									 bus_space_handle_t bsh, bus_size_t offset,
									 uint8_t value)
{
	if (tag == X86_BUS_SPACE_IO)
		outb(bsh + offset, value);
	else
		*(volatile uint8_t *)(bsh + offset) = value;
}

static inline void bus_space_write_2(bus_space_tag_t tag,
									 bus_space_handle_t bsh, bus_size_t offset,
									 uint16_t value)
{
	if (tag == X86_BUS_SPACE_IO)
		outw(bsh + offset, value);
	else
		*(volatile uint16_t *)(bsh + offset) = value;
}

static inline void bus_space_write_4(bus_space_tag_t tag,
									 bus_space_handle_t bsh, bus_size_t offset,
									 uint32_t value)
{
	if (tag == X86_BUS_SPACE_IO)
		outl(bsh + offset, value);
	else
		*(volatile uint32_t *)(bsh + offset) = value;
}

#endif /* ROS_BSD_BUS_H */
