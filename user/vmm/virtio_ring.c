/* Virtio ring implementation.
 *
 *  Copyright 2007 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdint.h>
#include <err.h>
#include <sys/mman.h>
#include <parlib/uthread.h>
#include <parlib/ros_debug.h>
#include <vmm/virtio.h>

#define BAD_RING(_vq, fmt, args...)				\
	do {							\
	fprintf(stderr, "%s:"fmt, (_vq)->vq.name, ##args);	\
	while (1); \
	} while (0)
/* Caller is supposed to guarantee no reentry. */
#define START_USE(_vq)						\
	do {							\
		if ((_vq)->in_use){				\
			fprintf(stderr, "%s:in_use = %i\n",	\
			      (_vq)->vq.name, (_vq)->in_use);	\
			exit(1);				\
		}						\
		(_vq)->in_use = __LINE__;			\
	} while (0)
#define BUG_ON(x) do { if (x) {fprintf(stderr, "bug\n"); while (1);} } while (0)
#define END_USE(_vq) \
	do { BUG_ON(!(_vq)->in_use); (_vq)->in_use = 0; } while(0)

int vringdebug = 0;

struct vring_virtqueue {
	struct virtqueue vq;

	/* Actual memory layout for this queue */
	struct vring vring;

	/* Can we use weak barriers? */
	bool weak_barriers;

	/* Other side has made a mess, don't try any more. */
	bool broken;

	/* Host supports indirect buffers */
	bool indirect;

	/* Host publishes avail event idx */
	bool event;

	/* Head of free buffer list. */
	unsigned int free_head;
	/* Number we've added since last sync. */
	unsigned int num_added;

	/* Last used index we've seen. */
	uint16_t last_used_idx;

	uint16_t last_avail_idx;

	/* How to notify other side. FIXME: commonalize hcalls! */
	 bool(*notify) (struct virtqueue * vq);

	/* They're supposed to lock for us. */
	unsigned int in_use;

	/* Figure out if their kicks are too delayed. */
	bool last_add_time_valid;
	uint64_t last_add_time;

	/* Tokens for callbacks. */
	void *data[];
};

/* Return the container/struct holding the object 'ptr' points to */

#define container_of(ptr, type, member) ({                                     \
	(type*)((char*)ptr - offsetof(type, member));                             \
})
#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)

static inline struct scatterlist *sg_next_chained(struct scatterlist *sg,
												  unsigned int *count)
{
	return NULL;
}

static inline struct scatterlist *sg_next_arr(struct scatterlist *sg,
											  unsigned int *count)
{
	if (--(*count) == 0)
		return NULL;
	return sg + 1;
}

/* Set up an indirect table of descriptors and add it to the queue. */
static inline int vring_add_indirect(struct vring_virtqueue *vq,
									 struct scatterlist *sgs[],
									 struct scatterlist *(*next)
									  (struct scatterlist *, unsigned int *),
									 unsigned int total_sg,
									 unsigned int total_out,
									 unsigned int total_in,
									 unsigned int out_sgs,
									 unsigned int in_sgs, int flags)
{
	struct vring_desc *desc;
	unsigned head;
	struct scatterlist *sg;
	int i, n;

	/*
	 * We require lowmem mappings for the descriptors because
	 * otherwise virt_to_phys will give us bogus addresses in the
	 * virtqueue.
	 */
	flags = 0;

	desc = calloc(total_sg, sizeof(struct vring_desc));
	if (!desc)
		return -ENOMEM;

	/* Transfer entries from the sg lists into the indirect page */
	i = 0;
	for (n = 0; n < out_sgs; n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_out)) {
			desc[i].flags = VRING_DESC_F_NEXT;
			desc[i].addr = sg_phys(sg->v);
			desc[i].len = sg->length;
			desc[i].next = i + 1;
			i++;
		}
	}
	for (; n < (out_sgs + in_sgs); n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_in)) {
			desc[i].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
			desc[i].addr = sg_phys(sg->v);
			desc[i].len = sg->length;
			desc[i].next = i + 1;
			i++;
		}
	}
	BUG_ON(i != total_sg);

	/* Last one doesn't continue. */
	desc[i - 1].flags &= ~VRING_DESC_F_NEXT;
	desc[i - 1].next = 0;

	/* We're about to use a buffer */
	vq->vq.num_free--;

	/* Use a single buffer which doesn't continue */
	head = vq->free_head;
	vq->vring.desc[head].flags = VRING_DESC_F_INDIRECT;
	vq->vring.desc[head].addr = virt_to_phys(desc);
	/* kmemleak gives a false positive, as it's hidden by virt_to_phys */
	//kmemleak_ignore(desc);
	vq->vring.desc[head].len = i * sizeof(struct vring_desc);

	/* Update free pointer */
	vq->free_head = vq->vring.desc[head].next;

	return head;
}

static inline int virtqueue_add_avail(struct virtqueue *_vq,
								struct scatterlist *sgs[],
								struct scatterlist *(*next)
								 (struct scatterlist *, unsigned int *),
								unsigned int total_out,
								unsigned int total_in,
								unsigned int out_sgs,
								unsigned int in_sgs, void *data, int flags)
{
	int canary;
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct scatterlist *sg;
	unsigned int i, n, avail, prev = 0, total_sg;
	int head;

	START_USE(vq);

	BUG_ON(data == NULL);

#ifdef DEBUG
	{
		ktime_t now = ktime_get();

		/* No kick or get, with .1 second between?  Warn. */
		if (vq->last_add_time_valid)
			WARN_ON(ktime_to_ms(ktime_sub(now, vq->last_add_time))
					> 100);
		vq->last_add_time = now;
		vq->last_add_time_valid = true;
	}
#endif

	total_sg = total_in + total_out;

	/* If the host supports indirect descriptor tables, and we have multiple
	 * buffers, then go indirect. FIXME: tune this threshold */
	if (vq->indirect && total_sg > 1 && vq->vq.num_free) {
		head = vring_add_indirect(vq, sgs, next, total_sg, total_out,
					  total_in, out_sgs, in_sgs, flags);
		if (head >= 0)
			goto add_head;
	}

	BUG_ON(total_sg > vq->vring.num);
	BUG_ON(total_sg == 0);
	canary = vq->vq.num_free;

	if (vq->vq.num_free < total_sg) {
		if (vringdebug) fprintf(stderr, "Can't add buf len %i - avail = %i\n",
				 total_sg, vq->vq.num_free);
		/* FIXME: for historical reasons, we force a notify here if
		 * there are outgoing parts to the buffer.  Presumably the
		 * host should service the ring ASAP. */
		if (out_sgs)
			vq->notify(&vq->vq);
		END_USE(vq);
		return -ENOSPC;
	}
	/* We're about to use some buffers from the free list. */
	vq->vq.num_free -= total_sg;

	head = i = vq->free_head;
	for (n = 0; n < out_sgs; n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_out)) {
			vq->vring.desc[i].flags = VRING_DESC_F_NEXT;
			vq->vring.desc[i].addr = sg_phys(sg->v);
			vq->vring.desc[i].len = sg->length;
			prev = i;
			i = vq->vring.desc[i].next;
		}
	}
	for (; n < (out_sgs + in_sgs); n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_in)) {
			vq->vring.desc[i].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
			vq->vring.desc[i].addr = sg_phys(sg->v);
			vq->vring.desc[i].len = sg->length;
			prev = i;
			i = vq->vring.desc[i].next;
		}
	}

	/* Last one doesn't continue. */
	vq->vring.desc[prev].flags &= ~VRING_DESC_F_NEXT;

	/* Update free pointer */
	vq->free_head = i;
add_head:
	/* Set token. */
	vq->data[head] = data;
	/* Put entry in available array (but don't update avail->idx until they
	 * do sync). */
	avail = (vq->vring.avail->idx & (vq->vring.num - 1));
	vq->vring.avail->ring[avail] = head;
	/* Descriptors and available array need to be set before we expose the
	 * new available array entries. */
	virtio_wmb(vq->weak_barriers);
	vq->vring.avail->idx++;
	vq->num_added++;

	/* This is very unlikely, but theoretically possible.  Kick
	 * just in case. */
	if (unlikely(vq->num_added == (1 << 16) - 1))
		virtqueue_kick(_vq);

	if (vringdebug) fprintf(stderr, "Added buffer head %i to %p\n", head, vq);
	END_USE(vq);
	BUG_ON(vq->vq.num_free > canary);
	return 0;
}

/**
 * virtqueue_add_outbuf_avail - expose output buffers to other end
 * @vq: the struct virtqueue we're talking about.
 * @sgs: array of scatterlists (need not be terminated!)
 * @num: the number of scatterlists readable by other side
 * @data: the token identifying the buffer.
 * @gfp: how to do memory allocations (if necessary).
 *
 * Caller must ensure we don't call this with other virtqueue operations
 * at the same time (except where noted).
 *
 * Returns zero or a negative error (ie. ENOSPC, ENOMEM).
 */
int virtqueue_add_outbuf_avail(struct virtqueue *vq,
						 struct scatterlist sg[], unsigned int num,
						 void *data, int flags)
{
	return virtqueue_add_avail(vq, &sg, sg_next_arr, num, 0, 1, 0, data, flags);
}

/**
 * virtqueue_add_inbuf_avail - expose input buffers to other end
 * @vq: the struct virtqueue we're talking about.
 * @sgs: array of scatterlists (need not be terminated!)
 * @num: the number of scatterlists writable by other side
 * @data: the token identifying the buffer.
 * @gfp: how to do memory allocations (if necessary).
 *
 * Caller must ensure we don't call this with other virtqueue operations
 * at the same time (except where noted).
 *
 * Returns zero or a negative error (ie. ENOSPC, ENOMEM).
 */
int virtqueue_add_inbuf_avail(struct virtqueue *vq,
						struct scatterlist sg[], unsigned int num,
						void *data, int flags)
{
	return virtqueue_add_avail(vq, &sg, sg_next_arr, 0, num, 0, 1, data, flags);
}

/**
 * virtqueue_kick_prepare - first half of split virtqueue_kick call.
 * @vq: the struct virtqueue
 *
 * Instead of virtqueue_kick(), you can do:
 *	if (virtqueue_kick_prepare(vq))
 *		virtqueue_notify(vq);
 *
 * This is sometimes useful because the virtqueue_kick_prepare() needs
 * to be serialized, but the actual virtqueue_notify() call does not.
 */
bool virtqueue_kick_prepare(struct virtqueue * _vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	uint16_t new, old;
	bool needs_kick;

	START_USE(vq);
	/* We need to expose available array entries before checking avail
	 * event. */
	virtio_mb(vq->weak_barriers);

	old = vq->vring.avail->idx - vq->num_added;
	new = vq->vring.avail->idx;
	vq->num_added = 0;

#ifdef DEBUG
	if (vq->last_add_time_valid) {
		WARN_ON(ktime_to_ms(ktime_sub(ktime_get(), vq->last_add_time)) > 100);
	}
	vq->last_add_time_valid = false;
#endif

	if (vq->event) {
		needs_kick = vring_need_event(vring_avail_event(&vq->vring), new, old);
	} else {
		needs_kick = !(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY);
	}
	END_USE(vq);
	return needs_kick;
}

/**
 * virtqueue_notify - second half of split virtqueue_kick call.
 * @vq: the struct virtqueue
 *
 * This does not need to be serialized.
 *
 * Returns false if host notify failed or queue is broken, otherwise true.
 */
bool virtqueue_notify(struct virtqueue * _vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	if (unlikely(vq->broken))
		return false;

	/* Prod other side to tell it about changes. */
	if (!vq->notify(_vq)) {
		vq->broken = true;
		return false;
	}
	return true;
}

/**
 * virtqueue_kick - update after add_buf
 * @vq: the struct virtqueue
 *
 * After one or more virtqueue_add_* calls, invoke this to kick
 * the other side.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns false if kick failed, otherwise true.
 */
bool virtqueue_kick(struct virtqueue * vq)
{
	if (virtqueue_kick_prepare(vq))
		return virtqueue_notify(vq);
	return true;
}

static void detach_buf(struct vring_virtqueue *vq, unsigned int head)
{
	unsigned int i;

	/* Clear data ptr. */
	vq->data[head] = NULL;

	/* Put back on free list: find end */
	i = head;

	/* Free the indirect table */
	if (vq->vring.desc[i].flags & VRING_DESC_F_INDIRECT) ;	//kfree(phys_to_virt(vq->vring.desc[i].addr));

	while (vq->vring.desc[i].flags & VRING_DESC_F_NEXT) {
		i = vq->vring.desc[i].next;
		vq->vq.num_free++;
	}

	vq->vring.desc[i].next = vq->free_head;
	vq->free_head = head;
	/* Plus final descriptor */
	vq->vq.num_free++;
}

static inline bool more_used(const struct vring_virtqueue *vq)
{
	return vq->last_used_idx != vq->vring.used->idx;
}

/**
 * virtqueue_get_buf_used - get the next used buffer
 * @vq: the struct virtqueue we're talking about.
 * @len: the length written into the buffer
 *
 * If the driver wrote data into the buffer, @len will be set to the
 * amount written.  This means you don't need to clear the buffer
 * beforehand to ensure there's no data leakage in the case of short
 * writes.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns NULL if there are no used buffers, or the "data" token
 * handed to virtqueue_add_*().
 */
void *virtqueue_get_buf_used(struct virtqueue *_vq, unsigned int *len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	void *ret;
	unsigned int i;
	uint16_t last_used;

	START_USE(vq);

	if (unlikely(vq->broken)) {
		END_USE(vq);
		return NULL;
	}

	if (!more_used(vq)) {
		if (vringdebug) fprintf(stderr, "No more buffers in queue\n");
		END_USE(vq);
		return NULL;
	}

	/* Only get used array entries after they have been exposed by host. */
	virtio_rmb(vq->weak_barriers);

	last_used = (vq->last_used_idx & (vq->vring.num - 1));
	i = vq->vring.used->ring[last_used].id;
	*len = vq->vring.used->ring[last_used].len;

	if (unlikely(i >= vq->vring.num)) {
		BAD_RING(vq, "id %u out of range\n", i);
		return NULL;
	}
	if (unlikely(!vq->data[i])) {
		BAD_RING(vq, "id %u is not a head!\n", i);
		return NULL;
	}

	/* detach_buf clears data, so grab it now. */
	ret = vq->data[i];
	detach_buf(vq, i);
	vq->last_used_idx++;
	/* If we expect an interrupt for the next entry, tell host
	 * by writing event index and flush out the write before
	 * the read in the next get_buf call. */
	if (!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
		vring_used_event(&vq->vring) = vq->last_used_idx;
		virtio_mb(vq->weak_barriers);
	}
#ifdef DEBUG
	vq->last_add_time_valid = false;
#endif

	END_USE(vq);
	return ret;
}

/**
 * virtqueue_disable_cb - disable callbacks
 * @vq: the struct virtqueue we're talking about.
 *
 * Note that this is not necessarily synchronous, hence unreliable and only
 * useful as an optimization.
 *
 * Unlike other operations, this need not be serialized.
 */
void virtqueue_disable_cb(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
}

/**
 * virtqueue_enable_cb_prepare - restart callbacks after disable_cb
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks; it returns current queue state
 * in an opaque unsigned value. This value should be later tested by
 * virtqueue_poll, to detect a possible race between the driver checking for
 * more work, and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
unsigned virtqueue_enable_cb_prepare(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	uint16_t last_used_idx;

	START_USE(vq);

	/* We optimistically turn back on interrupts, then check if there was
	 * more to do. */
	/* Depending on the VIRTIO_RING_F_EVENT_IDX feature, we need to
	 * either clear the flags bit or point the event index at the next
	 * entry. Always do both to keep code simple. */
	vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
	vring_used_event(&vq->vring) = last_used_idx = vq->last_used_idx;
	END_USE(vq);
	return last_used_idx;
}

/**
 * virtqueue_poll - query pending used buffers
 * @vq: the struct virtqueue we're talking about.
 * @last_used_idx: virtqueue state (from call to virtqueue_enable_cb_prepare).
 *
 * Returns "true" if there are pending used buffers in the queue.
 *
 * This does not need to be serialized.
 */
bool virtqueue_poll_used(struct virtqueue * _vq, unsigned last_used_idx)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	virtio_mb(vq->weak_barriers);
	return (uint16_t) last_used_idx != vq->vring.used->idx;
}

/**
 * virtqueue_enable_cb - restart callbacks after disable_cb.
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks; it returns "false" if there are pending
 * buffers in the queue, to detect a possible race between the driver
 * checking for more work, and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
bool virtqueue_enable_cb(struct virtqueue * _vq)
{
	unsigned last_used_idx = virtqueue_enable_cb_prepare(_vq);
	return !virtqueue_poll_used(_vq, last_used_idx);
}

/**
 * virtqueue_enable_cb_delayed - restart callbacks after disable_cb.
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks but hints to the other side to delay
 * interrupts until most of the available buffers have been processed;
 * it returns "false" if there are many pending buffers in the queue,
 * to detect a possible race between the driver checking for more work,
 * and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
bool virtqueue_enable_cb_delayed(struct virtqueue * _vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	uint16_t bufs;

	START_USE(vq);

	/* We optimistically turn back on interrupts, then check if there was
	 * more to do. */
	/* Depending on the VIRTIO_RING_F_USED_EVENT_IDX feature, we need to
	 * either clear the flags bit or point the event index at the next
	 * entry. Always do both to keep code simple. */
	vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
	/* TODO: tune this threshold */
	bufs = (uint16_t) (vq->vring.avail->idx - vq->last_used_idx) * 3 / 4;
	vring_used_event(&vq->vring) = vq->last_used_idx + bufs;
	virtio_mb(vq->weak_barriers);
	if (unlikely((uint16_t) (vq->vring.used->idx - vq->last_used_idx) > bufs)) {
		END_USE(vq);
		return false;
	}

	END_USE(vq);
	return true;
}

/**
 * virtqueue_detach_unused_buf - detach first unused buffer
 * @vq: the struct virtqueue we're talking about.
 *
 * Returns NULL or the "data" token handed to virtqueue_add_*().
 * This is not valid on an active queue; it is useful only for device
 * shutdown.
 */
void *virtqueue_detach_unused_buf(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	unsigned int i;
	void *buf;

	START_USE(vq);

	for (i = 0; i < vq->vring.num; i++) {
		if (!vq->data[i])
			continue;
		/* detach_buf clears data, so grab it now. */
		buf = vq->data[i];
		detach_buf(vq, i);
		vq->vring.avail->idx--;
		END_USE(vq);
		return buf;
	}
	/* That should have freed everything. */
	BUG_ON(vq->vq.num_free != vq->vring.num);

	END_USE(vq);
	return NULL;
}

struct virtqueue *vring_new_virtqueue(unsigned int index,
									  unsigned int num,
									  unsigned int vring_align,
									  bool weak_barriers,
									  void *pages,
									  bool(*notify) (struct virtqueue *),
									  void (*callback) (struct virtqueue *),
									  const char *name)
{
	struct vring_virtqueue *vq;
	unsigned int i;

	/* We assume num is a power of 2. */
	if (num & (num - 1)) {
		if (vringdebug) fprintf(stderr, "Bad virtqueue length %u\n", num);
		exit(1);
	}

	vq = mmap((int*)4096, sizeof(*vq) + sizeof(void *) * num + 2*PGSIZE, PROT_READ | PROT_WRITE,
		  MAP_ANONYMOUS, -1, 0);
	if (vq == MAP_FAILED) {
		perror("Unable to mmap vq");
		exit(1);
	}
	fprintf(stderr, "VQ %p %d bytes \n", vq, num * vring_align); /* really? */
	if (!vq)
		return NULL;

	// I *think* they correctly offset from vq for the vring? 
	vring_init(&vq->vring, num, pages, vring_align);
	fprintf(stderr, "done vring init\n");
	vq->vq.callback = callback;
	vq->vq.name = name;
	vq->vq.num_free = num;
	vq->vq.index = index;
	vq->notify = notify;
	vq->weak_barriers = weak_barriers;
	vq->broken = false;
	vq->last_used_idx = 0;
	vq->num_added = 0;
#ifdef DEBUG
	vq->in_use = false;
	vq->last_add_time_valid = false;
#endif

	vq->indirect = 0;	// virtio_has_feature(vdev, VIRTIO_RING_F_INDIRECT_DESC);
	vq->event = 0;	//virtio_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX);

	/* No callback?  Tell other side not to bother us. */
	if (!callback)
		vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

	/* Put everything in free lists. */
	vq->free_head = 0;
	for (i = 0; i < num - 1; i++) {
		vq->vring.desc[i].next = i + 1;
		vq->data[i] = NULL;
	}
	vq->data[i] = NULL;

	return &vq->vq;
}

void vring_del_virtqueue(struct virtqueue *vq)
{

}

#if 0
/* Manipulates transport-specific feature bits. */
void vring_transport_features(struct virtio_device *vdev)
{
	unsigned int i;

	for (i = VIRTIO_TRANSPORT_F_START; i < VIRTIO_TRANSPORT_F_END; i++) {
		switch (i) {
			case VIRTIO_RING_F_INDIRECT_DESC:
				break;
			case VIRTIO_RING_F_EVENT_IDX:
				break;
			default:
				/* We don't understand this bit. */
				clear_bit(i, vdev->features);
		}
	}
}
#endif
/**
 * virtqueue_get_vring_size - return the size of the virtqueue's vring
 * @vq: the struct virtqueue containing the vring of interest.
 *
 * Returns the size of the vring.  This is mainly used for boasting to
 * userspace.  Unlike other operations, this need not be serialized.
 */
unsigned int virtqueue_get_vring_size(struct virtqueue *_vq)
{

	struct vring_virtqueue *vq = to_vvq(_vq);

	return vq->vring.num;
}

bool virtqueue_is_broken(struct virtqueue * _vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	return vq->broken;
}


/* new stuff. This is now more symmetric. */

int avail(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	return vq->vring.avail->idx;
}

void showvq(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct vring_desc *desc = vq->vring.desc;
	struct vring_avail *a = vq->vring.avail;
	struct vring_used *u = vq->vring.used;

//	int i;
	fprintf(stderr, "vq %p, desc %p, avail %p, used %p broken %d\n", vq, desc, a, u, vq->broken);
	fprintf(stderr, "vq; %s, index 0x%x, num_free 0x%x, priv %p\n", vq->vq.name, vq->vq.index, vq->vq.num_free, vq->vq.priv);
	fprintf(stderr, "avail: flags 0x%x idx 0x%x\n", a->flags, a->idx);
	fprintf(stderr, "used: flags 0x%x idx 0x%x \n", u->flags, u->idx);
}

void showdesc(struct virtqueue *_vq, uint16_t head)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct vring_desc *d = &vq->vring.desc[head];

	while (1) {
		fprintf(stderr, "%d(%p): %p 0x%x 0x%x[", head, d, (void *)d->addr, d->len, d->flags);
		if (d->flags & VRING_DESC_F_WRITE)
			fprintf(stderr, "W");
		else
			fprintf(stderr, "R");
		if (d->flags & VRING_DESC_F_INDIRECT)
			fprintf(stderr, "I");
		fprintf(stderr, "]");
		if (d->flags & VRING_DESC_F_NEXT)
			fprintf(stderr, "->0x%x,", d->next);
		else
			break;
		d++;
		head++;
		head = head & (vq->vring.num - 1);
	}
	fprintf(stderr, "\n");

}

/* This gets the next entry in the queue as one sglist.
 * we should probably mirror the
 * in linux virtio they have all kinds of tricks they play to avoid
 * vmexits and such on virtio. I don't care. The whole point of this is
 * to avoid vmexits but leave the VMMCP active on the cores. We're
 * going to be in a core rich world and putting in timesharing hacks
 * makes no sense.
 */
int virtio_get_buf_avail_start(struct virtqueue *_vq, uint16_t *last_avail_idx, struct scatterlist **sgp, int *sgplen)
{
	struct scatterlist *sg;
	struct vring_virtqueue *vq = to_vvq(_vq);
	uint16_t avail_idx, i, head;
	int sglen;
//	int err;

	avail_idx = vq->vring.avail->idx;

	if (*last_avail_idx == avail_idx)
		return vq->vring.num;

	/* coherence here: Only get avail ring entries after they have been exposed by guest. */

	i = *last_avail_idx & (vq->vring.num - 1);

	head = vq->vring.avail->ring[i];

	if (head >= vq->vring.num) {
		if (vringdebug) fprintf(stderr, "Guest says index %u > %u is available",
			   head, vq->vring.num);
		return -EINVAL;
	}

	(*last_avail_idx)++;

	struct vring_desc *d = vq->vring.desc;
	for(i = head, sglen = 1; d[i].flags & VRING_DESC_F_NEXT; sglen++) {
		i++;
		i = i & (vq->vring.num - 1);
	}

	if (sgp) {
		if (vringdebug) fprintf(stderr, "entry @%d is %d long\n", head, sglen);

		sg = calloc(sglen, sizeof(*sg));
		*sgp = sg;
		*sgplen = sglen;

		for(i = head; sglen; sglen--) {
			sg->v = (void *)d[i].addr;
			sg->length = d[i].len;
			i++;
			sg++;
			i = i & (vq->vring.num - 1);
		}
	}
	return head;
}


void virtio_get_buf_avail_done(struct virtqueue *_vq, uint16_t last_avail_idx, int id, int len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	/* consume it. */
	struct vring_used *u = vq->vring.used;
	u->ring[u->idx].id = id;
	u->ring[u->idx].len = len;
	u->idx = (u->idx + 1) % vq->vring.num;
}

#define lg_last_avail(vq)	((vq)->last_avail_idx)
#define guest_limit ((1ULL<<48)-1)
/*L:200
 * Device Handling.
 *
 * When the Guest gives us a buffer, it sends an array of addresses and sizes.
 * We need to make sure it's not trying to reach into the Launcher itself, so
 * we have a convenient routine which checks it and exits with an error message
 * if something funny is going on:
 */
static void *_check_pointer(unsigned long addr, unsigned int size,
			    unsigned int line)
{
	/*
	 * Check if the requested address and size exceeds the allocated memory,
	 * or addr + size wraps around.
	 */
	if ((addr + size) > guest_limit || (addr + size) < addr)
		errx(1, "%s:%i: Invalid address %#lx", __FILE__, line, addr);
	/*
	 * We return a pointer for the caller's convenience, now we know it's
	 * safe to use.
	 */
	return (void *)addr;
}
/* A macro which transparently hands the line number to the real function. */
#define check_pointer(addr,size) _check_pointer(addr, size, __LINE__)

/*
 * Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain, or vq->vring.num if we're
 * at the end.
 */
static unsigned next_desc(struct vring_desc *desc,
			  unsigned int i, unsigned int max)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(desc[i].flags & VRING_DESC_F_NEXT))
		return max;

	/* Check they're not leading us off end of descriptors. */
	next = desc[i].next;
	/* Make sure compiler knows to grab that: we don't want it changing! */
	wmb();

	if (next >= max)
		errx(1, "Desc next is %u", next);

	return next;
}

/*
 * This looks in the virtqueue for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function waits if necessary, and returns the descriptor number found.
 */
unsigned int wait_for_vq_desc(struct virtqueue *_vq,
				 struct scatterlist iov[],
				 unsigned int *out_num, unsigned int *in_num)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	unsigned int i, head, max;
	struct vring_desc *desc;
	uint16_t last_avail = lg_last_avail(vq);
	int j = 0;

	if (vringdebug){
		fprintf(stderr, "out_num %p in_num %p\n", out_num, in_num);
		fprintf(stderr, "va %p vq->vring %p\n", vq, vq->vring);
	}
	*out_num = *in_num = 0;
	/* There's nothing available? */
	j = 0;
	while (last_avail == vq->vring.avail->idx) {
		//uint64_t event;
		if (virtqueue_is_broken(_vq)) {
			return 0;
		}

		/*
		 * Since we're about to sleep, now is a good time to tell the
		 * Guest about what we've used up to now.

		trigger_irq(vq);
		 */
		/* OK, now we need to know about added descriptors. */
		vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;

		/*
		 * They could have slipped one in as we were doing that: make
		 * sure it's written, then check again.
		 */
		mb();
		if (last_avail != vq->vring.avail->idx) {
			vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
			break;
		}

		/* Nothing new?  Wait for eventfd to tell us they refilled. *
		if (read(vq->eventfd, &event, sizeof(event)) != sizeof(event))
			errx(1, "Event read failed?");
		*/
		/* We don't need to be notified again. */
		vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
	}

	/* Check it isn't doing very strange things with descriptor numbers. */
//showvq(_vq);
//fprintf(stderr, "out of loop %d.", j++); (void)getchar();
	if ((uint16_t)(vq->vring.avail->idx - last_avail) > vq->vring.num)
		errx(1, "Guest moved used index from %u to %u",
		     last_avail, vq->vring.avail->idx);

	/*
	 * Make sure we read the descriptor number *after* we read the ring
	 * update; don't let the cpu or compiler change the order.
	 */
	rmb();

	/*
	 * Grab the next descriptor number they're advertising, and increment
	 * the index we've seen.
	 */
	head = vq->vring.avail->ring[last_avail % vq->vring.num];
	lg_last_avail(vq)++;

	/* If their number is silly, that's a fatal mistake. */
	if (head >= vq->vring.num)
		errx(1, "Guest says index %u is available", head);

	/* When we start there are none of either input nor output. */
	*out_num = *in_num = 0;

	max = vq->vring.num;
	desc = vq->vring.desc;
	i = head;

	/*
	 * We have to read the descriptor after we read the descriptor number,
	 * but there's a data dependency there so the CPU shouldn't reorder
	 * that: no rmb() required.
	 */

	/*
	 * If this is an indirect entry, then this buffer contains a descriptor
	 * table which we handle as if it's any normal descriptor chain.
	 */
	if (desc[i].flags & VRING_DESC_F_INDIRECT) {
		if (desc[i].len % sizeof(struct vring_desc))
			errx(1, "Invalid size for indirect buffer table");

		max = desc[i].len / sizeof(struct vring_desc);
		// take our chances.
		desc = check_pointer(desc[i].addr, desc[i].len);
		i = 0;
	}


	do {
		/* Grab the first descriptor, and check it's OK. */
		iov[*out_num + *in_num].length = desc[i].len;
		iov[*out_num + *in_num].v
			= check_pointer(desc[i].addr, desc[i].len);
		/* If this is an input descriptor, increment that count. */
		if (desc[i].flags & VRING_DESC_F_WRITE)
			(*in_num)++;
		else {
			/*
			 * If it's an output descriptor, they're all supposed
			 * to come before any input descriptors.
			 */
			if (*in_num)
				errx(1, "Descriptor has out after in");
			(*out_num)++;
		}

		/* If we've got too many, that implies a descriptor loop. */
		if (*out_num + *in_num > max)
			errx(1, "Looped descriptor");
	} while ((i = next_desc(desc, i, max)) != max);

	if (vringdebug) fprintf(stderr, "RETURN head %d\n", head); 
	return head;
}

/*
 * After we've used one of their buffers, we tell the Guest about it.  Sometime
 * later we'll want to send them an interrupt using trigger_irq(); note that
 * wait_for_vq_desc() does that for us if it has to wait.
 */
void add_used(struct virtqueue *_vq, unsigned int head, int len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct vring_used_elem *used;

	/*
	 * The virtqueue contains a ring of used buffers.  Get a pointer to the
	 * next entry in that used ring.
	 */
	used = &vq->vring.used->ring[vq->vring.used->idx % vq->vring.num];
	used->id = head;
	used->len = len;
	/* Make sure buffer is written before we update index. */
	wmb();
	vq->vring.used->idx++;
	if (vringdebug) fprintf(stderr, "USED IDX is now %d\n", vq->vring.used->idx);
	//vq->pending_used++;
}

void showscatterlist(struct scatterlist *sg, int num)
{
	int i;
	fprintf(stderr, "%p(0x%x)[", sg, num);
	for(i = 0; i < num; i++)
		fprintf(stderr, "[%p, 0x%x],", sg[i].v, sg[i].length);
	fprintf(stderr, "]\n");
}

void virtqueue_close(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	vq->broken = true;
}
