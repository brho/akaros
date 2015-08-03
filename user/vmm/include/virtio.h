#ifndef VIRTIO_VIRTIO_H
#define VIRTIO_VIRTIO_H

#include <ros/arch/membar.h>
#include <ros/arch/mmu.h>
#include <ros/virtio_ring.h>
#include <ros/common.h>

#include <stddef.h>
#include <stdbool.h>

/* this is just an iov but we're going to keep the type for now, in case
 * we want it at some point.
 */
struct scatterlist {
	void *v;
	int length;
};

#define unlikely(x) (x)
#define virtio_rmb(x) rmb()
#define virtio_wmb(x) wmb()
#define virtio_mb(x) mb()

#define sg_phys(x) ((uintptr_t)x)
#define virt_to_phys(x) ((uintptr_t)x)

struct virtqueue *vring_new_virtqueue(unsigned int index,
				      unsigned int num,
				      unsigned int vring_align,
				      bool weak_barriers,
				      void *pages,
				      bool (*notify)(struct virtqueue *),
				      void (*callback)(struct virtqueue *),
				      const char *name);

int virtqueue_add_outbuf_avail(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			       int opts); // Opts only used in kernel functions.
unsigned int virtqueue_get_vring_size(struct virtqueue *_vq);
void *virtqueue_get_buf_used(struct virtqueue *_vq, unsigned int *len);
int virtqueue_add_inbuf_avail(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			      void *data, int opts);

/* linux-isms we may or may not ever care about. */
#define __user
#define __force
#define __cold

int avail(struct virtqueue *_vq);
void showvq(struct virtqueue *_vq);
void showdesc(struct virtqueue *_vq, uint16_t head);
int virtio_get_buf_avail_start(struct virtqueue *_vq, uint16_t *last_avail_idx, struct scatterlist **sgp, int *sgplen);
void virtio_get_buf_avail_done(struct virtqueue *_vq, uint16_t last_avail_idx, int id, int len);
void showscatterlist(struct scatterlist *sg, int num);

unsigned int wait_for_vq_desc(struct virtqueue *vq,
				 struct scatterlist iov[],
				 unsigned int *out_num, unsigned int *in_num);
void add_used(struct virtqueue *vq, unsigned int head, int len);

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @list: the chain of virtqueues for this device
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @name: the name of this virtqueue (mainly for debugging)
 * @vdev: the virtio device this queue was created for.
 * @priv: a pointer for the virtqueue implementation to use.
 * @index: the zero-based ordinal number for this queue.
 * @num_free: number of elements we expect to be able to fit.
 *
 * A note on @num_free: with indirect buffers, each buffer needs one
 * element in the queue, otherwise a buffer will need one element per
 * sg element.
 */
struct virtqueue {
	void (*callback)(struct virtqueue *vq);
	const char *name;
	unsigned int index;
	unsigned int num_free;
	void *priv;
};

int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			 int flags);

int virtqueue_add_inbuf(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			void *data,
			int flags);

int virtqueue_add_sgs(struct virtqueue *vq,
		      struct scatterlist *sgs[],
		      unsigned int out_sgs,
		      unsigned int in_sgs,
		      void *data,
		      int flags);

bool virtqueue_kick(struct virtqueue *vq);

bool virtqueue_kick_prepare(struct virtqueue *vq);

bool virtqueue_notify(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);

unsigned virtqueue_enable_cb_prepare(struct virtqueue *vq);

bool virtqueue_poll(struct virtqueue *vq, unsigned);

bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

unsigned int virtqueue_get_vring_size(struct virtqueue *vq);

bool virtqueue_is_broken(struct virtqueue *vq);
void virtqueue_close(struct virtqueue *vq);

static inline uint32_t read32(const volatile void *addr)
{
	return *(const volatile uint32_t *)addr;
}

static inline void write32(volatile void *addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

#endif /* VIRTIO_VIRTIO_H */
