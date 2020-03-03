/* Copyright (c) 2019 Google Inc
 * Aditya Basu <mitthu@google.com>
 * Barret Rhoden <brho@google.com>
 * See LICENSE for details.
 */

#include <parlib/stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFFERSIZE 20

#define error_exit(s)		\
do {				\
	perror((s));		\
	exit(-1);		\
} while (1)

/* Descriptor structue as defined in the programmer's guide.
 * It describes a single DMA transfer
 */
struct ucbdma {
	uint64_t		dst_addr;
	uint64_t		src_addr;
	uint32_t		xfer_size;
	char			bdf_str[10];
} __attribute__((packed));

static void *map_page(void)
{
	void *region;
	size_t pagesize = getpagesize();

	region = mmap(0, pagesize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_ANON | MAP_PRIVATE, 0, 0);

	if (region == MAP_FAILED)
		error_exit("cannot mmap");

	return region;
}

static void unmap_page(void *region)
{
	int err;
	size_t pagesize = getpagesize();

	err = munmap(region, pagesize);
	if (err)
		error_exit("cannot munmap");
}

static void issue_dma(struct ucbdma *ptr)
{
	int fd = open("#cbdma/ucopy", O_RDWR);

	if (fd < 0)
		error_exit("open failed: #cbdma/ucopy");

	printf("[user] ucbdma ptr: %p\n", ptr);
	if (write(fd, ptr, sizeof(struct ucbdma)) < 0)
		error_exit("write ucbdma");

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
	printf("[user] \txref_size: %d\n", ucbdma->xfer_size);
	printf("[user] \tsrc_addr: %p\n", ucbdma->src_addr);
	printf("[user] \tdst_addr: %p\n", ucbdma->dst_addr);
}

static void attach_device(char *pcistr)
{
	char buf[1024];
	int fd = open("#iommu/attach", O_RDWR);

	if (fd < 0)
		error_exit("open failed: #iommu/attach");

	sprintf(buf, "%s %d\n", pcistr, getpid());
	if (write(fd, buf, strlen(buf)) < 0)
		error_exit("attach");
	close(fd);

	system("cat \\#iommu/mappings");
}

static void detach_device(char *pcistr)
{
	char buf[1024];
	int fd = open("#iommu/detach", O_RDWR);

	if (fd < 0)
		error_exit("open failed: #iommu/detach");

	sprintf(buf, "%s %d\n", pcistr, getpid());
	if (write(fd, buf, strlen(buf)) < 0)
		error_exit("dettach");
	close(fd);
}

static void showmapping(pid_t pid, char *dst)
{
	/* One could imagine typeof-based macros that create a string of the
	 * right size and snprintf variables with %d, %p, whatever... */
	char pid_s[20];
	char addr_s[20];
	char *argv[] = { "m", "showmapping", pid_s, addr_s, NULL };

	snprintf(pid_s, sizeof(pid_s), "%d", pid);
	snprintf(addr_s, sizeof(addr_s), "%p", dst);

	run_and_wait(argv[0], sizeof(argv), argv);
}

int main(int argc, char *argv[])
{
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

	/* setup src and dst buffers; 100 is random padding */
	src = map_page();
	dst = map_page();
	printf("[user] mmaped src %p\n", src);
	printf("[user] mmaped dst %p\n", dst);
	/* No need to fill dst, it is all zeros (\0, not '0') from the OS */
	fill_buffer(src, '1', BUFFERSIZE);
	printf("[user] src: %s\n", src);
	printf("[user] dst: %s\n", dst);

	/* setup ucbdma*/
	ucbdma = malloc(sizeof(struct ucbdma));
	ucbdma->xfer_size = BUFFERSIZE;
	ucbdma->src_addr  = (uint64_t) src;
	ucbdma->dst_addr = (uint64_t) dst;
	memcpy(&ucbdma->bdf_str, pcistr, sizeof(ucbdma->bdf_str));

	issue_dma(ucbdma);

	printf("[user] src: %s\n", src);
	printf("[user] dst: %s\n", dst);

	/* Force an IOTLB flush by mmaping/munmapping an arbitrary page */
	unmap_page(map_page());

	/* Ideally, we'd see the dirty bit set in the PTE.  But we probably
	 * won't.  The user would have to dirty the page to tell the OS it was
	 * dirtied, which is really nasty. */
	printf("[user] Asking the kernel to show the PTE for %p\n", dst);
	showmapping(getpid(), dst);

	/* cleanup */
	unmap_page(src);
	unmap_page(dst);

	detach_device(pcistr);

	return 0;
}
