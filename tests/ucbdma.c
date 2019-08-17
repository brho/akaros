/* Copyright (c) 2019 Google Inc
 * Aditya Basu <mitthu@google.com>
 * See LICENSE for details.

 * For kernel space
 * ----------------
 * uintptr_t uva2kva(struct proc *p, void *uva, size_t len, int prot)
 * prot is e.g. PROT_WRITE (writable by userspace).
 * returns a KVA, which you can convert to a phys addr with PADDR().
 *
 * TODO:
 *   - Bypass DMA re-mapping if iommu is not turned on (in #cbdma/iommu).
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <parlib/assert.h>

#define CBDMA_DESC_CTRL_INTR_ON_COMPLETION              0x00000001
#define CBDMA_DESC_CTRL_WRITE_CHANCMP_ON_COMPLETION     0x00000008
#define CBDMA_DESC_CTRL_NULL_DESC                       0x20

#define BUFFERSIZE 20

/* Descriptor structue as defined in the programmer's guide.
 * It describes a single DMA transfer
 */
struct desc {
	uint32_t  xfer_size;
	uint32_t  descriptor_control;
	uint64_t  src_addr;
	uint64_t  dest_addr;
	uint64_t  next_desc_addr;
	uint64_t  next_source_address;
	uint64_t  next_destination_address;
	uint64_t  reserved0;
	uint64_t  reserved1;
} __attribute__((packed));

/* describe a DMA */
struct ucbdma {
	struct desc    desc;
	uint64_t       status;
	uint16_t       ndesc;
};

static void *map_page(void)
{
	void *region;
	size_t pagesize = getpagesize();

	printf("[user] page size: %zu bytes\n", pagesize);

	region = mmap(0, pagesize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_ANON | MAP_PRIVATE, 0, 0);

	if (region == MAP_FAILED)
		panic("cannot mmap");

	return region;
}

static void unmap_page(void *region)
{
	int err;
	size_t pagesize = getpagesize();

	err = munmap(region, pagesize);
	if (err)
		panic("cannot munmap");
}

static void issue_dma(struct ucbdma *ptr)
{
	int fd = open("#cbdma/ucopy", O_RDWR);

	if (fd < 0)
		panic("open failed: #cbdma/ucopy");

	printf("[user] ucbdma ptr: %p\n", ptr);
	write(fd, ptr, sizeof(struct ucbdma *));

	close(fd);
}

static void fill_buffer(char *buffer, char c, int size)
{
	memset(buffer, c, size);
	buffer[size-1] = '\0';
}

static void dump_ucbdma(struct ucbdma *ucbdma)
{
	printf("[user] ucbdma: %p, size: %d (or 0x%x)\n", ucbdma,
		sizeof(struct ucbdma), sizeof(struct ucbdma));
	printf("[user] \tdesc->xref_size: %d\n", ucbdma->desc.xfer_size);
	printf("[user] \tdesc->src_addr: %p\n", ucbdma->desc.src_addr);
	printf("[user] \tdesc->dest_addr: %p\n", ucbdma->desc.dest_addr);
	printf("[user] \tdesc->next_desc_addr: %p\n",
		ucbdma->desc.next_desc_addr);
	printf("[user] \tndesc: %d\n", ucbdma->ndesc);
	printf("[user] \tstatus: 0x%llx\n", ucbdma->status);
}

static void attach_device(char *pcistr)
{
	char buf[1024];
	int fd = open("#iommu/attach", O_RDWR);

	if (fd < 0)
		panic("open failed: #iommu/attach");

	sprintf(buf, "%s %d\n", pcistr, getpid());
	write(fd, buf, strlen(buf));

	close(fd);

	system("cat \\#iommu/mappings");
}

static void detach_device(char *pcistr)
{
	int fd = open("#iommu/detach", O_RDWR);

	if (fd < 0)
		panic("open failed: #iommu/detach");

	write(fd, pcistr, strlen(pcistr));

	close(fd);
}

int main(int argc, char **argv)
{
	char *region;
	struct ucbdma *ucbdma;
	char *src, *dst;
	char *pcistr;

	if (argc < 2) {
		printf("example:\n");
		printf("\tucbdma 00:04.7\n");
		exit(1);
	}
	pcistr = argv[1];

	attach_device(pcistr);

	/* map page for placing ucbdma */
	region = map_page();

	/* setup src and dst buffers; 100 is random padding */
	src = region + sizeof(struct ucbdma) + 100;
	dst = region + sizeof(struct ucbdma) + 100 + BUFFERSIZE;
	fill_buffer(src, '1', BUFFERSIZE);
	fill_buffer(dst, '0', BUFFERSIZE);
	printf("[user] src: %s\n", src);
	printf("[user] dst: %s\n", dst);

	/* setup ucbdma*/
	ucbdma = (struct ucbdma *) region;
	ucbdma->status = 0;
	ucbdma->desc.descriptor_control
		= CBDMA_DESC_CTRL_INTR_ON_COMPLETION
		| CBDMA_DESC_CTRL_WRITE_CHANCMP_ON_COMPLETION;
	ucbdma->desc.xfer_size = BUFFERSIZE;
	ucbdma->desc.src_addr  = (uint64_t) src;
	ucbdma->desc.dest_addr = (uint64_t) dst;
	ucbdma->desc.next_desc_addr = (uint64_t) &ucbdma->desc;
	ucbdma->ndesc = 1;

	dump_ucbdma(ucbdma);
	issue_dma(ucbdma);
	dump_ucbdma(ucbdma);

	printf("[user] channel_status: %llx\n", ucbdma->status);
	printf("[user] src: %s\n", src);
	printf("[user] dst: %s\n", dst);

	/* cleanup */
	unmap_page(region);

	detach_device(pcistr);

	return 0;
}
