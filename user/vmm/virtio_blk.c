#define _LARGEFILE64_SOURCE /* See feature_test_macros(7) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vmm/virtio.h>
#include <vmm/virtio_blk.h>
#include <vmm/virtio_mmio.h>

int debug_virtio_blk;

#define DPRINTF(fmt, ...)                                                      \
	do {                                                                       \
	if (debug_virtio_blk) {                                                    \
		fprintf(stderr, "virtio_blk: " fmt, ##__VA_ARGS__);                    \
	}                                                                          \
	} while (0)

/* TODO(ganshun): multiple disks */
static int diskfd;

void blk_init_fn(struct virtio_vq_dev *vqdev, const char *filename)
{
	struct virtio_blk_config *cfg = vqdev->cfg;
	struct virtio_blk_config *cfg_d = vqdev->cfg_d;
	uint64_t len;
	struct stat stat_result;

	diskfd = open(filename, O_RDWR);
	if (diskfd < 0)
		VIRTIO_DEV_ERRX(vqdev, "Could not open disk image file %s", filename);

	if (stat(filename, &stat_result) == -1)
		VIRTIO_DEV_ERRX(vqdev, "Could not stat file %s", filename);
	len = stat_result.st_size / 512;

	cfg->capacity = len;
	cfg_d->capacity = len;
}

void *blk_request(void *_vq)
{
	struct virtio_vq *vq = _vq;

	assert(vq != NULL);

	struct virtio_mmio_dev *dev = vq->vqdev->transport_dev;
	struct iovec *iov;
	uint32_t head;
	uint32_t olen, ilen;
	struct virtio_blk_outhdr *out;
	uint64_t offset;
	int64_t ret;
	size_t wlen;
	uint8_t *status;
	struct virtio_blk_config *cfg = vq->vqdev->cfg;

	if (vq->qready != 0x1)
		VIRTIO_DEV_ERRX(vq->vqdev,
		                "The service function for queue '%s' was launched before the driver set QueueReady to 0x1.",
		                 vq->name);

	if (!dev->poke_guest)
		VIRTIO_DEV_ERRX(vq->vqdev,
		                "The 'poke_guest' function pointer was not set.");

	iov = malloc(vq->qnum_max * sizeof(struct iovec));
	if (iov == NULL)
		VIRTIO_DEV_ERRX(vq->vqdev,
		                "malloc returned null trying to allocate iov.\n");

	for (;;) {
		head = virtio_next_avail_vq_desc(vq, iov, &olen, &ilen);
		/* There are always three iovecs.
		 * The first is the header.
		 * The second is the actual data.
		 * The third contains just the status byte.
		 */

		status = iov[2].iov_base;
		if (!status)
			VIRTIO_DEV_ERRX(vq->vqdev, "no room for status\n");

		out = iov[0].iov_base;
		if (out->type & VIRTIO_BLK_T_FLUSH)
			VIRTIO_DEV_ERRX(vq->vqdev, "Flush not supported.\n");

		offset = out->sector * 512;
		if (lseek64(diskfd, offset, SEEK_SET) != offset)
			VIRTIO_DEV_ERRX(vq->vqdev, "Bad seek at sector %llu\n",
			                out->sector);

		if (out->type & VIRTIO_BLK_T_OUT) {

			if ((offset + iov[1].iov_len) > (cfg->capacity * 512))
				VIRTIO_DEV_ERRX(vq->vqdev, "write past end of file!\n");

			ret = writev(diskfd, &iov[1], 1);

			if (ret >= 0 && ret == iov[1].iov_len)
				*status = VIRTIO_BLK_S_OK;
			else
				*status = VIRTIO_BLK_S_IOERR;
			wlen = sizeof(*status);
		} else {
			ret = readv(diskfd, &iov[1], 1);
			if (ret >= 0) {
				wlen = sizeof(*status) + ret;
				*status = VIRTIO_BLK_S_OK;
			} else {
				wlen = sizeof(*status);
				*status = VIRTIO_BLK_S_IOERR;
			}

			// Hexdump for debugging.
			if (debug_virtio_blk && ret >= 0) {
				char *pf = "";

				for (int i = 0; i < iov[olen].iov_len; i += 2) {
					uint8_t *p = (uint8_t *)iov[olen].iov_base + i;

					fprintf(stderr, "%s%02x", pf, *(p + 1));
					fprintf(stderr, "%02x", *p);
					fprintf(stderr, " ");
					pf = ((i + 2) % 16) ? " " : "\n";
				}
			}
		}

		virtio_add_used_desc(vq, head, wlen);
		virtio_mmio_set_vring_irq(dev);
		dev->poke_guest(dev->vec, dev->dest);
	}
	return 0;
}
