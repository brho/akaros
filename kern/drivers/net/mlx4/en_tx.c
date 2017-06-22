/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/mlx4/cq.h>
#include <linux/mlx4/qp.h>
#include "mlx4_en.h"

int mlx4_en_create_tx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_tx_ring **pring, uint32_t size,
			   uint16_t stride, int node, int queue_index)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_tx_ring *ring;
	int tmp;
	int err;

	ring = kzalloc_node(sizeof(*ring), MEM_WAIT, node);
	if (!ring) {
		ring = kzmalloc(sizeof(*ring), MEM_WAIT);
		if (!ring) {
			en_err(priv, "Failed allocating TX ring\n");
			return -ENOMEM;
		}
	}

	ring->size = size;
	ring->size_mask = size - 1;
	ring->stride = stride;

	tmp = size * sizeof(struct mlx4_en_tx_info);
	ring->tx_info = kmalloc_node(tmp, MEM_WAIT | __GFP_NOWARN, node);
	if (!ring->tx_info) {
		ring->tx_info = vmalloc(tmp);
		if (!ring->tx_info) {
			err = -ENOMEM;
			goto err_ring;
		}
	}

	en_dbg(DRV, priv, "Allocated tx_info ring at addr:%p size:%d\n",
		 ring->tx_info, tmp);

	ring->bounce_buf = kmalloc_node(MAX_DESC_SIZE, MEM_WAIT, node);
	if (!ring->bounce_buf) {
		ring->bounce_buf = kmalloc(MAX_DESC_SIZE, MEM_WAIT);
		if (!ring->bounce_buf) {
			err = -ENOMEM;
			goto err_info;
		}
	}
	ring->buf_size = ALIGN(size * ring->stride, MLX4_EN_PAGE_SIZE);

	/* Allocate HW buffers on provided NUMA node */
	set_dev_node(&mdev->dev->persist->pdev->dev, node);
	err = mlx4_alloc_hwq_res(mdev->dev, &ring->wqres, ring->buf_size,
				 2 * PAGE_SIZE);
	set_dev_node(&mdev->dev->persist->pdev->dev, mdev->dev->numa_node);
	if (err) {
		en_err(priv, "Failed allocating hwq resources\n");
		goto err_bounce;
	}

	err = mlx4_en_map_buffer(&ring->wqres.buf);
	if (err) {
		en_err(priv, "Failed to map TX buffer\n");
		goto err_hwq_res;
	}

	ring->buf = ring->wqres.buf.direct.buf;

	en_dbg(DRV, priv, "Allocated TX ring (addr:%p) - buf:%p size:%d buf_size:%d dma:%llx\n",
	       ring, ring->buf, ring->size, ring->buf_size,
	       (unsigned long long) ring->wqres.buf.direct.map);

	err = mlx4_qp_reserve_range(mdev->dev, 1, 1, &ring->qpn,
				    MLX4_RESERVE_ETH_BF_QP);
	if (err) {
		en_err(priv, "failed reserving qp for TX ring\n");
		goto err_map;
	}

	err = mlx4_qp_alloc(mdev->dev, ring->qpn, &ring->qp, MEM_WAIT);
	if (err) {
		en_err(priv, "Failed allocating qp %d\n", ring->qpn);
		goto err_reserve;
	}
	ring->qp.event = mlx4_en_sqp_event;

#if 0 // AKAROS_PORT
	err = mlx4_bf_alloc(mdev->dev, &ring->bf, node);
	if (err) {
#else
	if (true) {
#endif
		en_dbg(DRV, priv, "working without blueflame (%d)\n", err);
		ring->bf.uar = &mdev->priv_uar;
		ring->bf.uar->map = mdev->uar_map;
		ring->bf_enabled = false;
		ring->bf_alloced = false;
		priv->pflags &= ~MLX4_EN_PRIV_FLAGS_BLUEFLAME;
	} else {
		ring->bf_alloced = true;
		ring->bf_enabled = !!(priv->pflags &
				      MLX4_EN_PRIV_FLAGS_BLUEFLAME);
	}

	ring->hwtstamp_tx_type = priv->hwtstamp_config.tx_type;
	ring->queue_index = queue_index;

	if (queue_index < priv->num_tx_rings_p_up)
		cpumask_set_cpu(cpumask_local_spread(queue_index,
						     priv->mdev->dev->numa_node),
				&ring->affinity_mask);

	*pring = ring;
	return 0;

err_reserve:
	mlx4_qp_release_range(mdev->dev, ring->qpn, 1);
err_map:
	mlx4_en_unmap_buffer(&ring->wqres.buf);
err_hwq_res:
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
err_bounce:
	kfree(ring->bounce_buf);
	ring->bounce_buf = NULL;
err_info:
#if 0 // AKAROS_PORT
	kvfree(ring->tx_info);
#endif
	ring->tx_info = NULL;
err_ring:
	kfree(ring);
	*pring = NULL;
	return err;
}

void mlx4_en_destroy_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring **pring)
{
	panic("Disabled");
#if 0 // AKAROS_PORT
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_tx_ring *ring = *pring;
	en_dbg(DRV, priv, "Destroying tx ring, qpn: %d\n", ring->qpn);

	if (ring->bf_alloced)
		mlx4_bf_free(mdev->dev, &ring->bf);
	mlx4_qp_remove(mdev->dev, &ring->qp);
	mlx4_qp_free(mdev->dev, &ring->qp);
	mlx4_en_unmap_buffer(&ring->wqres.buf);
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
	kfree(ring->bounce_buf);
	ring->bounce_buf = NULL;
	kvfree(ring->tx_info);
	ring->tx_info = NULL;
	kfree(ring);
	*pring = NULL;
#endif
}

int mlx4_en_activate_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring *ring,
			     int cq, int user_prio)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	ring->cqn = cq;
	ring->prod = 0;
	ring->cons = 0xffffffff;
	ring->last_nr_txbb = 1;
	memset(ring->tx_info, 0, ring->size * sizeof(struct mlx4_en_tx_info));
	memset(ring->buf, 0, ring->buf_size);

	ring->qp_state = MLX4_QP_STATE_RST;
	ring->doorbell_qpn = cpu_to_be32(ring->qp.qpn << 8);
	ring->mr_key = cpu_to_be32(mdev->mr.key);

	mlx4_en_fill_qp_context(priv, ring->size, ring->stride, 1, 0, ring->qpn,
				ring->cqn, user_prio, &ring->context);
	if (ring->bf_alloced)
		ring->context.usr_page = cpu_to_be32(ring->bf.uar->index);

	err = mlx4_qp_to_ready(mdev->dev, &ring->wqres.mtt, &ring->context,
			       &ring->qp, &ring->qp_state);
#if 0 // AKAROS_PORT
	if (!cpumask_empty(&ring->affinity_mask))
		netif_set_xps_queue(priv->dev, &ring->affinity_mask,
				    ring->queue_index);
#endif
	poke_init(&ring->poker, __mlx4_xmit_poke);

	return err;
}

void mlx4_en_deactivate_tx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring)
{
	struct mlx4_en_dev *mdev = priv->mdev;

	mlx4_qp_modify(mdev->dev, NULL, ring->qp_state,
		       MLX4_QP_STATE_RST, NULL, 0, 0, &ring->qp);
}

static void mlx4_en_stamp_wqe(struct mlx4_en_priv *priv,
			      struct mlx4_en_tx_ring *ring, int index,
			      uint8_t owner)
{
	__be32 stamp = cpu_to_be32(STAMP_VAL | (!!owner << STAMP_SHIFT));
	struct mlx4_en_tx_desc *tx_desc = ring->buf + index * TXBB_SIZE;
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	void *end = ring->buf + ring->buf_size;
	__be32 *ptr = (__be32 *)tx_desc;
	int i;

	/* Optimize the common case when there are no wraparounds */
	if (likely((void *)tx_desc + tx_info->nr_txbb * TXBB_SIZE <= end)) {
		/* Stamp the freed descriptor */
		for (i = 0; i < tx_info->nr_txbb * TXBB_SIZE;
		     i += STAMP_STRIDE) {
			*ptr = stamp;
			ptr += STAMP_DWORDS;
		}
	} else {
		/* Stamp the freed descriptor */
		for (i = 0; i < tx_info->nr_txbb * TXBB_SIZE;
		     i += STAMP_STRIDE) {
			*ptr = stamp;
			ptr += STAMP_DWORDS;
			if ((void *)ptr >= end) {
				ptr = ring->buf;
				stamp ^= cpu_to_be32(0x80000000);
			}
		}
	}
}


static uint32_t mlx4_en_free_tx_desc(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring,
				int index, uint8_t owner, uint64_t timestamp)
{
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	struct mlx4_en_tx_desc *tx_desc = ring->buf + index * TXBB_SIZE;
	struct mlx4_wqe_data_seg *data = (void *) tx_desc + tx_info->data_offset;
	void *end = ring->buf + ring->buf_size;
	struct block *block = tx_info->block;
	int nr_maps = tx_info->nr_maps;
	int i;

#if 0 // AKAROS_PORT
	/* We do not touch skb here, so prefetch skb->users location
	 * to speedup consume_skb()
	 */
	prefetchw(&skb->users);

	if (unlikely(timestamp)) {
		struct skb_shared_hwtstamps hwts;

		mlx4_en_fill_hwtstamps(priv->mdev, &hwts, timestamp);
		skb_tstamp_tx(skb, &hwts);
	}
#endif

	/* Optimize the common case when there are no wraparounds */
	if (likely((void *) tx_desc + tx_info->nr_txbb * TXBB_SIZE <= end)) {
		if (!tx_info->inl) {
			if (tx_info->linear)
				dma_unmap_single(priv->ddev,
						tx_info->map0_dma,
						tx_info->map0_byte_count,
						PCI_DMA_TODEVICE);
			else
				dma_unmap_page(priv->ddev,
					       tx_info->map0_dma,
					       tx_info->map0_byte_count,
					       PCI_DMA_TODEVICE);
			for (i = 1; i < nr_maps; i++) {
				data++;
				dma_unmap_page(priv->ddev,
					(dma_addr_t)be64_to_cpu(data->addr),
					be32_to_cpu(data->byte_count),
					PCI_DMA_TODEVICE);
			}
		}
	} else {
		if (!tx_info->inl) {
			if ((void *) data >= end) {
				data = ring->buf + ((void *)data - end);
			}

			if (tx_info->linear)
				dma_unmap_single(priv->ddev,
						tx_info->map0_dma,
						tx_info->map0_byte_count,
						PCI_DMA_TODEVICE);
			else
				dma_unmap_page(priv->ddev,
					       tx_info->map0_dma,
					       tx_info->map0_byte_count,
					       PCI_DMA_TODEVICE);
			for (i = 1; i < nr_maps; i++) {
				data++;
				/* Check for wraparound before unmapping */
				if ((void *) data >= end)
					data = ring->buf;
				dma_unmap_page(priv->ddev,
					(dma_addr_t)be64_to_cpu(data->addr),
					be32_to_cpu(data->byte_count),
					PCI_DMA_TODEVICE);
			}
		}
	}
	freeb(block);
	return tx_info->nr_txbb;
}


int mlx4_en_free_tx_buf(struct ether *dev, struct mlx4_en_tx_ring *ring)
{
	panic("Disabled");
#if 0 // AKAROS_PORT
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int cnt = 0;

	/* Skip last polled descriptor */
	ring->cons += ring->last_nr_txbb;
	en_dbg(DRV, priv, "Freeing Tx buf - cons:0x%x prod:0x%x\n",
		 ring->cons, ring->prod);

	if ((uint32_t) (ring->prod - ring->cons) > ring->size) {
		if (netif_msg_tx_err(priv))
			en_warn(priv, "Tx consumer passed producer!\n");
		return 0;
	}

	while (ring->cons != ring->prod) {
		ring->last_nr_txbb = mlx4_en_free_tx_desc(priv, ring,
						ring->cons & ring->size_mask,
						!!(ring->cons & ring->size), 0);
		ring->cons += ring->last_nr_txbb;
		cnt++;
	}

	netdev_tx_reset_queue(ring->tx_queue);

	if (cnt)
		en_dbg(DRV, priv, "Freed %d uncompleted tx descriptors\n", cnt);

	return cnt;
#endif
}

static bool mlx4_en_process_tx_cq(struct ether *dev,
				  struct mlx4_en_cq *cq)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_cq *mcq = &cq->mcq;
	struct mlx4_en_tx_ring *ring = priv->tx_ring[cq->ring];
	struct mlx4_cqe *cqe;
	uint16_t index;
	uint16_t new_index, ring_index, stamp_index;
	uint32_t txbbs_skipped = 0;
	uint32_t txbbs_stamp = 0;
	uint32_t cons_index = mcq->cons_index;
	int size = cq->size;
	uint32_t size_mask = ring->size_mask;
	struct mlx4_cqe *buf = cq->buf;
	uint32_t packets = 0;
	uint32_t bytes = 0;
	int factor = priv->cqe_factor;
	uint64_t timestamp = 0;
	int done = 0;
	int budget = priv->tx_work_limit;
	uint32_t last_nr_txbb;
	uint32_t ring_cons;

	if (!priv->port_up)
		return true;

#if 0 // AKAROS_PORT
	netdev_txq_bql_complete_prefetchw(ring->tx_queue);
#endif

	index = cons_index & size_mask;
	cqe = mlx4_en_get_cqe(buf, index, priv->cqe_size) + factor;
	last_nr_txbb = ACCESS_ONCE(ring->last_nr_txbb);
	ring_cons = ACCESS_ONCE(ring->cons);
	ring_index = ring_cons & size_mask;
	stamp_index = ring_index;

	/* Process all completed CQEs */
	while (XNOR(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK,
			cons_index & size) && (done < budget)) {
		/*
		 * make sure we read the CQE after we read the
		 * ownership bit
		 */
		bus_rmb();

		if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
			     MLX4_CQE_OPCODE_ERROR)) {
			struct mlx4_err_cqe *cqe_err = (struct mlx4_err_cqe *)cqe;

			en_err(priv, "CQE error - vendor syndrome: 0x%x syndrome: 0x%x\n",
			       cqe_err->vendor_err_syndrome,
			       cqe_err->syndrome);
		}

		/* Skip over last polled CQE */
		new_index = be16_to_cpu(cqe->wqe_index) & size_mask;

		do {
			txbbs_skipped += last_nr_txbb;
			ring_index = (ring_index + last_nr_txbb) & size_mask;
			if (ring->tx_info[ring_index].ts_requested)
				timestamp = mlx4_en_get_cqe_ts(cqe);

			/* free next descriptor */
			last_nr_txbb = mlx4_en_free_tx_desc(
					priv, ring, ring_index,
					!!((ring_cons + txbbs_skipped) &
					ring->size), timestamp);

			mlx4_en_stamp_wqe(priv, ring, stamp_index,
					  !!((ring_cons + txbbs_stamp) &
						ring->size));
			stamp_index = ring_index;
			txbbs_stamp = txbbs_skipped;
			packets++;
			bytes += ring->tx_info[ring_index].nr_bytes;
		} while ((++done < budget) && (ring_index != new_index));

		++cons_index;
		index = cons_index & size_mask;
		cqe = mlx4_en_get_cqe(buf, index, priv->cqe_size) + factor;
	}


	/*
	 * To prevent CQ overflow we first update CQ consumer and only then
	 * the ring consumer.
	 */
	mcq->cons_index = cons_index;
	mlx4_cq_set_ci(mcq);
	wmb();

	/* we want to dirty this cache line once */
	ACCESS_ONCE(ring->last_nr_txbb) = last_nr_txbb;
	ACCESS_ONCE(ring->cons) = ring_cons + txbbs_skipped;

#if 0 // AKAROS_PORT
	netdev_tx_completed_queue(ring->tx_queue, packets, bytes);

	/*
	 * Wakeup Tx queue if this stopped, and at least 1 packet
	 * was completed
	 */
	if (netif_tx_queue_stopped(ring->tx_queue) && txbbs_skipped > 0) {
		netif_tx_wake_queue(ring->tx_queue);
		ring->wake_queue++;
	}
#else
	struct mlx4_poke_args args;

	if (txbbs_skipped > 0) {
		args.edev = dev;
		args.priv = priv;
		args.ring = ring;
		poke(&ring->poker, &args);
	}
#endif
	return done < budget;
}

static void mlx4_en_poll_tx_cq(uint32_t srcid, long a0, long a1, long a2);

void mlx4_en_tx_irq(struct mlx4_cq *mcq)
{
	struct mlx4_en_cq *cq = container_of(mcq, struct mlx4_en_cq, mcq);
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);

	if (likely(priv->port_up))
#if 0 // AKAROS_PORT
		napi_schedule_irqoff(&cq->napi);
#else
		send_kernel_message(core_id(), mlx4_en_poll_tx_cq, (long)cq,
				    0, 0, KMSG_ROUTINE);
#endif
	else
		mlx4_en_arm_cq(priv, cq);
}

/* TX CQ polling - called by NAPI */
static void mlx4_en_poll_tx_cq(uint32_t srcid, long a0, long a1, long a2)
{
	struct mlx4_en_cq *cq = (struct mlx4_en_cq *)a0;
	struct ether *dev = cq->dev;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int clean_complete;

	clean_complete = mlx4_en_process_tx_cq(dev, cq);
	if (!clean_complete)
		return;

	mlx4_en_arm_cq(priv, cq);
}

static struct mlx4_en_tx_desc *mlx4_en_bounce_to_desc(struct mlx4_en_priv *priv,
						      struct mlx4_en_tx_ring *ring,
						      uint32_t index,
						      unsigned int desc_size)
{
	uint32_t copy = (ring->size - index) * TXBB_SIZE;
	int i;

	for (i = desc_size - copy - 4; i >= 0; i -= 4) {
		if ((i & (TXBB_SIZE - 1)) == 0)
			wmb();

		*((uint32_t *) (ring->buf + i)) =
			*((uint32_t *) (ring->bounce_buf + copy + i));
	}

	for (i = copy - 4; i >= 4 ; i -= 4) {
		if ((i & (TXBB_SIZE - 1)) == 0)
			wmb();

		*((uint32_t *) (ring->buf + index * TXBB_SIZE + i)) =
			*((uint32_t *) (ring->bounce_buf + i));
	}

	/* Return real descriptor location */
	return ring->buf + index * TXBB_SIZE;
}

#if 0 // AKAROS_PORT
/* Decide if skb can be inlined in tx descriptor to avoid dma mapping
 *
 * It seems strange we do not simply use skb_copy_bits().
 * This would allow to inline all skbs iff skb->len <= inline_thold
 *
 * Note that caller already checked skb was not a gso packet
 */
static bool is_inline(int inline_thold, const struct sk_buff *skb,
		      const struct skb_shared_info *shinfo,
		      void **pfrag)
{
	void *ptr;

	if (skb->len > inline_thold || !inline_thold)
		return false;

	if (shinfo->nr_frags == 1) {
		ptr = skb_frag_address_safe(&shinfo->frags[0]);
		if (unlikely(!ptr))
			return false;
		*pfrag = ptr;
		return true;
	}
	if (shinfo->nr_frags)
		return false;
	return true;
}

static int inline_size(const struct sk_buff *skb)
{
	if (skb->len + CTRL_SIZE + sizeof(struct mlx4_wqe_inline_seg)
	    <= MLX4_INLINE_ALIGN)
		return ALIGN(skb->len + CTRL_SIZE +
			     sizeof(struct mlx4_wqe_inline_seg), 16);
	else
		return ALIGN(skb->len + CTRL_SIZE + 2 *
			     sizeof(struct mlx4_wqe_inline_seg), 16);
}

static int get_real_size(const struct sk_buff *skb,
			 const struct skb_shared_info *shinfo,
			 struct ether *dev,
			 int *lso_header_size,
			 bool *inline_ok,
			 void **pfrag)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int real_size;

	if (shinfo->gso_size) {
		*inline_ok = false;
		if (skb->encapsulation)
			*lso_header_size = (skb_inner_transport_header(skb) - skb->data) + inner_tcp_hdrlen(skb);
		else
			*lso_header_size = skb_transport_offset(skb) + tcp_hdrlen(skb);
		real_size = CTRL_SIZE + shinfo->nr_frags * DS_SIZE +
			ALIGN(*lso_header_size + 4, DS_SIZE);
		if (unlikely(*lso_header_size != skb_headlen(skb))) {
			/* We add a segment for the skb linear buffer only if
			 * it contains data */
			if (*lso_header_size < skb_headlen(skb))
				real_size += DS_SIZE;
			else {
				if (netif_msg_tx_err(priv))
					en_warn(priv, "Non-linear headers\n");
				return 0;
			}
		}
	} else {
		*lso_header_size = 0;
		*inline_ok = is_inline(priv->prof->inline_thold, skb,
				       shinfo, pfrag);

		if (*inline_ok)
			real_size = inline_size(skb);
		else
			real_size = CTRL_SIZE +
				    (shinfo->nr_frags + 1) * DS_SIZE;
	}

	return real_size;
}

static void build_inline_wqe(struct mlx4_en_tx_desc *tx_desc,
			     const struct sk_buff *skb,
			     const struct skb_shared_info *shinfo,
			     int real_size, uint16_t *vlan_tag,
			     int tx_ind, void *fragptr)
{
	struct mlx4_wqe_inline_seg *inl = &tx_desc->inl;
	int spc = MLX4_INLINE_ALIGN - CTRL_SIZE - sizeof *inl;
	unsigned int hlen = skb_headlen(skb);

	if (skb->len <= spc) {
		if (likely(skb->len >= MIN_PKT_LEN)) {
			inl->byte_count = cpu_to_be32(1 << 31 | skb->len);
		} else {
			inl->byte_count = cpu_to_be32(1 << 31 | MIN_PKT_LEN);
			memset(((void *)(inl + 1)) + skb->len, 0,
			       MIN_PKT_LEN - skb->len);
		}
		skb_copy_from_linear_data(skb, inl + 1, hlen);
		if (shinfo->nr_frags)
			memcpy(((void *)(inl + 1)) + hlen, fragptr,
			       skb_frag_size(&shinfo->frags[0]));

	} else {
		inl->byte_count = cpu_to_be32(1 << 31 | spc);
		if (hlen <= spc) {
			skb_copy_from_linear_data(skb, inl + 1, hlen);
			if (hlen < spc) {
				memcpy(((void *)(inl + 1)) + hlen,
				       fragptr, spc - hlen);
				fragptr +=  spc - hlen;
			}
			inl = (void *) (inl + 1) + spc;
			memcpy(((void *)(inl + 1)), fragptr, skb->len - spc);
		} else {
			skb_copy_from_linear_data(skb, inl + 1, spc);
			inl = (void *) (inl + 1) + spc;
			skb_copy_from_linear_data_offset(skb, spc, inl + 1,
							 hlen - spc);
			if (shinfo->nr_frags)
				memcpy(((void *)(inl + 1)) + hlen - spc,
				       fragptr,
				       skb_frag_size(&shinfo->frags[0]));
		}

		bus_wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (skb->len - spc));
	}
}
#endif

uint16_t mlx4_en_select_queue(struct ether *dev, struct sk_buff *skb,
			      void *accel_priv,
			      select_queue_fallback_t fallback)
{
	panic("Disabled");
#if 0 // AKAROS_PORT
	struct mlx4_en_priv *priv = netdev_priv(dev);
	uint16_t rings_p_up = priv->num_tx_rings_p_up;
	uint8_t up = 0;

	if (dev->num_tc)
		return skb_tx_hash(dev, skb);

	if (skb_vlan_tag_present(skb))
		up = skb_vlan_tag_get(skb) >> VLAN_PRIO_SHIFT;

	return fallback(dev, skb) % rings_p_up + up * rings_p_up;
#endif
}

#if 0 // AKAROS_PORT
static void mlx4_bf_copy(void __iomem *dst, const void *src,
			 unsigned int bytecnt)
{
	__iowrite64_copy(dst, src, bytecnt / 8);
}
#endif

/* Akaros's TCP stack always puts the TCP header in the block main body, so we
 * don't need to worry about those being in extra_data.  Linux stores the
 * location of the transport header and whatnot in the SKB - we do the same. */
static size_t get_lso_hdr_size(struct block *block)
{
	if (!(block->flag & Btso))
		return 0;
	return block->transport_header_end;
}

netdev_tx_t mlx4_send_packet(struct block *block, struct mlx4_en_priv *priv,
                             struct mlx4_en_tx_ring *ring)
{
	struct mlx4_en_tx_desc *tx_desc;
	struct mlx4_wqe_data_seg *data;
	struct mlx4_en_tx_info *tx_info;
	int nr_txbb;
	int desc_size;
	int real_size;
	uint32_t index;
	__be32 op_own;
	int i_frag;
	int nr_frags = 0;
	bool bounce = false;
	dma_addr_t dma = 0;
	uint32_t byte_count = 0;
	size_t lso_header_size;
	/* linear there is something to send in the block header */
	bool is_linear;

	if (!priv->port_up)
		goto tx_drop;

	lso_header_size = get_lso_hdr_size(block);
	for (i_frag = 0; i_frag < block->nr_extra_bufs; i_frag++) {
		const struct extra_bdata *ebd;

		ebd = &block->extra_data[i_frag];
		if (ebd->base && ebd->len > 0)
			nr_frags++;
	}
	/* Transport stack should always put the packet headers in the main body. */
	assert(!(lso_header_size > BHLEN(block)));
	/* == means there is nothing in the block main body other than the headers.
	 * in which case, we won't need an extra data_seg. */
	is_linear = lso_header_size < BHLEN(block);

	real_size = CTRL_SIZE + nr_frags * DS_SIZE;
	if (is_linear)
		real_size += DS_SIZE;
	if (lso_header_size)
		real_size += ALIGN(lso_header_size + 4, DS_SIZE);
	if (unlikely(!real_size))
		goto tx_drop;

	/* Align descriptor to TXBB size */
	desc_size = ALIGN(real_size, TXBB_SIZE);
	nr_txbb = desc_size / TXBB_SIZE;
	if (unlikely(nr_txbb > MAX_DESC_TXBBS)) {
		en_warn(priv, "Oversized header or SG list\n");
		goto tx_drop;
	}

	index = ring->prod & ring->size_mask;

	/* See if we have enough space for whole descriptor TXBB for setting
	 * SW ownership on next descriptor; if not, use a bounce buffer. */
	if (likely(index + nr_txbb <= ring->size))
		tx_desc = ring->buf + index * TXBB_SIZE;
	else {
		tx_desc = (struct mlx4_en_tx_desc *) ring->bounce_buf;
		bounce = true;
	}

	/* Save skb in tx_info ring */
	tx_info = &ring->tx_info[index];
	tx_info->block = block;
	tx_info->nr_txbb = nr_txbb;

	data = &tx_desc->data;
	if (lso_header_size)
		data = ((void *)&tx_desc->lso + ALIGN(lso_header_size + 4,
						      DS_SIZE));

	/* valid only for none inline segments */
	tx_info->data_offset = (void *)data - (void *)tx_desc;
	tx_info->inl = 0;
	tx_info->linear = is_linear ? 1 : 0;
	tx_info->nr_maps = nr_frags + tx_info->linear;
	data += tx_info->nr_maps - 1;

	/* Map fragments if any */
	for (i_frag = block->nr_extra_bufs - 1; i_frag >= 0; i_frag--) {
		const struct extra_bdata *ebd;

		ebd = &block->extra_data[i_frag];
		if (!ebd->base || ebd->len <= 0)
			continue;

		byte_count = ebd->len;
		dma = dma_map_single(0, (void *)(ebd->base + ebd->off),
				     byte_count, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(0, dma)))
			goto tx_drop_unmap;

		data->addr = cpu_to_be64(dma);
		data->lkey = ring->mr_key;
		bus_wmb();
		data->byte_count = cpu_to_be32(byte_count);
		--data;
	}

	if (tx_info->linear) {
		byte_count = BHLEN(block) - lso_header_size;

		dma = dma_map_single(0, block->rp + lso_header_size, byte_count,
		                     DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(0, dma)))
			goto tx_drop_unmap;

		data->addr = cpu_to_be64(dma);
		data->lkey = ring->mr_key;
		bus_wmb();
		data->byte_count = cpu_to_be32(byte_count);
	}

	/* tx completion can avoid cache line miss for common cases */
	tx_info->map0_dma = dma;
	tx_info->map0_byte_count = byte_count;

	/*
	 * For timestamping add flag to skb_shinfo and
	 * set flag for further reference
	 */
	tx_info->ts_requested = 0;

	/* Prepare ctrl segement apart opcode+ownership */
	tx_desc->ctrl.srcrb_flags = priv->ctrl_flags;
	if (likely(block->flag & BCKSUM_FLAGS)) {
		assert(block->flag & (Budpck | Btcpck));
		tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM |
		                                         MLX4_WQE_CTRL_TCP_UDP_CSUM);
		ring->tx_csum++;
	}

	if (priv->flags & MLX4_EN_FLAG_ENABLE_HW_LOOPBACK) {
		struct ethhdr *ethh;

		/* Copy dst mac address to wqe. This allows loopback in eSwitch,
		 * so that VFs and PF can communicate with each other
		 */
		ethh = (struct ethhdr *)block->rp;
		tx_desc->ctrl.srcrb_flags16[0] = get_unaligned((__be16 *)ethh->h_dest);
		tx_desc->ctrl.imm = get_unaligned((__be32 *)(ethh->h_dest + 2));
	}

	/* Handle LSO (TSO) packets */
	if (lso_header_size) {
		int i;

		/* Mark opcode as LSO */
		op_own = cpu_to_be32(MLX4_OPCODE_LSO | (1 << 6)) |
			((ring->prod & ring->size) ?
				cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0);

		/* Fill in the LSO prefix */
		tx_desc->lso.mss_hdr_size = cpu_to_be32(block->mss << 16 |
		                                        lso_header_size);

		/* Copy headers;
		 * note that we already verified that it is linear.
		 * brho - meaning that the lso_header_size is within block->rp. */
		memcpy(tx_desc->lso.header, block->rp, lso_header_size);

		ring->tso_packets++;

		i = ((BLEN(block) - lso_header_size) / block->mss) +
			!!((BLEN(block) - lso_header_size) % block->mss);
		tx_info->nr_bytes = BLEN(block) + (i - 1) * lso_header_size;
		ring->packets += i;
	} else {

		/* Normal (Non LSO) packet */
		op_own = cpu_to_be32(MLX4_OPCODE_SEND) |
			((ring->prod & ring->size) ?
			 cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0);
		tx_info->nr_bytes = MAX_T(unsigned int, BLEN(block), ETH_ZLEN);
		ring->packets++;
	}
	ring->bytes += tx_info->nr_bytes;
	AVG_PERF_COUNTER(priv->pstats.tx_pktsz_avg, BLEN(block));

	ring->prod += nr_txbb;

	/* If we used a bounce buffer then copy descriptor back into place */
	if (unlikely(bounce))
		tx_desc = mlx4_en_bounce_to_desc(priv, ring, index, desc_size);

	/* Flow control is handled by the mlx4_transmit function in main.c */

	real_size = (real_size / 16) & 0x3f; /* Clear fence bit. */

	tx_desc->ctrl.vlan_tag = 0;
	tx_desc->ctrl.ins_vlan = 0;
	tx_desc->ctrl.fence_size = real_size;

	/* Ensure new descriptor hits memory
	 * before setting ownership of this descriptor to HW
	 */
	bus_wmb();
	tx_desc->ctrl.owner_opcode = op_own;
	wmb();
	/* Since there is no iowrite*_native() that writes the
	 * value as is, without byteswapping - using the one
	 * the doesn't do byteswapping in the relevant arch
	 * endianness.
	 */
#if defined(__LITTLE_ENDIAN)
	write32(ring->doorbell_qpn, ring->bf.uar->map + MLX4_SEND_DOORBELL);
#else
	iowrite32be(ring->doorbell_qpn,
		    ring->bf.uar->map + MLX4_SEND_DOORBELL);
#endif

	return NETDEV_TX_OK;

tx_drop_unmap:
	en_err(priv, "DMA mapping error\n");

tx_drop:
	priv->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

#if 0 // AKAROS_PORT
netdev_tx_t mlx4_en_xmit(struct sk_buff *skb, struct ether *dev)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct device *ddev = priv->ddev;
	struct mlx4_en_tx_ring *ring;
	struct mlx4_en_tx_desc *tx_desc;
	struct mlx4_wqe_data_seg *data;
	struct mlx4_en_tx_info *tx_info;
	int tx_ind = 0;
	int nr_txbb;
	int desc_size;
	int real_size;
	uint32_t index, bf_index;
	__be32 op_own;
	uint16_t vlan_tag = 0;
	int i_frag;
	int lso_header_size;
	void *fragptr = NULL;
	bool bounce = false;
	bool send_doorbell;
	bool stop_queue;
	bool inline_ok;
	uint32_t ring_cons;

	if (!priv->port_up)
		goto tx_drop;

	tx_ind = skb_get_queue_mapping(skb);
	ring = priv->tx_ring[tx_ind];

	/* fetch ring->cons far ahead before needing it to avoid stall */
	ring_cons = ACCESS_ONCE(ring->cons);

	real_size = get_real_size(skb, shinfo, dev, &lso_header_size,
				  &inline_ok, &fragptr);
	if (unlikely(!real_size))
		goto tx_drop;

	/* Align descriptor to TXBB size */
	desc_size = ALIGN(real_size, TXBB_SIZE);
	nr_txbb = desc_size / TXBB_SIZE;
	if (unlikely(nr_txbb > MAX_DESC_TXBBS)) {
		if (netif_msg_tx_err(priv))
			en_warn(priv, "Oversized header or SG list\n");
		goto tx_drop;
	}

	if (skb_vlan_tag_present(skb))
		vlan_tag = skb_vlan_tag_get(skb);


	netdev_txq_bql_enqueue_prefetchw(ring->tx_queue);

	/* Track current inflight packets for performance analysis */
	AVG_PERF_COUNTER(priv->pstats.inflight_avg,
			 (uint32_t)(ring->prod - ring_cons - 1));

	/* Packet is good - grab an index and transmit it */
	index = ring->prod & ring->size_mask;
	bf_index = ring->prod;

	/* See if we have enough space for whole descriptor TXBB for setting
	 * SW ownership on next descriptor; if not, use a bounce buffer. */
	if (likely(index + nr_txbb <= ring->size))
		tx_desc = ring->buf + index * TXBB_SIZE;
	else {
		tx_desc = (struct mlx4_en_tx_desc *) ring->bounce_buf;
		bounce = true;
	}

	/* Save skb in tx_info ring */
	tx_info = &ring->tx_info[index];
	tx_info->skb = skb;
	tx_info->nr_txbb = nr_txbb;

	data = &tx_desc->data;
	if (lso_header_size)
		data = ((void *)&tx_desc->lso + ALIGN(lso_header_size + 4,
						      DS_SIZE));

	/* valid only for none inline segments */
	tx_info->data_offset = (void *)data - (void *)tx_desc;

	tx_info->inl = inline_ok;

	tx_info->linear = (lso_header_size < skb_headlen(skb) &&
			   !inline_ok) ? 1 : 0;

	tx_info->nr_maps = shinfo->nr_frags + tx_info->linear;
	data += tx_info->nr_maps - 1;

	if (!tx_info->inl) {
		dma_addr_t dma = 0;
		uint32_t byte_count = 0;

		/* Map fragments if any */
		for (i_frag = shinfo->nr_frags - 1; i_frag >= 0; i_frag--) {
			const struct skb_frag_struct *frag;

			frag = &shinfo->frags[i_frag];
			byte_count = skb_frag_size(frag);
			dma = skb_frag_dma_map(ddev, frag,
					       0, byte_count,
					       DMA_TO_DEVICE);
			if (dma_mapping_error(ddev, dma))
				goto tx_drop_unmap;

			data->addr = cpu_to_be64(dma);
			data->lkey = ring->mr_key;
			bus_wmb();
			data->byte_count = cpu_to_be32(byte_count);
			--data;
		}

		/* Map linear part if needed */
		if (tx_info->linear) {
			byte_count = skb_headlen(skb) - lso_header_size;

			dma = dma_map_single(ddev, skb->data +
					     lso_header_size, byte_count,
					     PCI_DMA_TODEVICE);
			if (dma_mapping_error(ddev, dma))
				goto tx_drop_unmap;

			data->addr = cpu_to_be64(dma);
			data->lkey = ring->mr_key;
			bus_wmb();
			data->byte_count = cpu_to_be32(byte_count);
		}
		/* tx completion can avoid cache line miss for common cases */
		tx_info->map0_dma = dma;
		tx_info->map0_byte_count = byte_count;
	}

	/*
	 * For timestamping add flag to skb_shinfo and
	 * set flag for further reference
	 */
	tx_info->ts_requested = 0;
	if (unlikely(ring->hwtstamp_tx_type == HWTSTAMP_TX_ON &&
		     shinfo->tx_flags & SKBTX_HW_TSTAMP)) {
		shinfo->tx_flags |= SKBTX_IN_PROGRESS;
		tx_info->ts_requested = 1;
	}

	/* Prepare ctrl segement apart opcode+ownership, which depends on
	 * whether LSO is used */
	tx_desc->ctrl.srcrb_flags = priv->ctrl_flags;
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		if (!skb->encapsulation)
			tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM |
								 MLX4_WQE_CTRL_TCP_UDP_CSUM);
		else
			tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM);
		ring->tx_csum++;
	}

	if (priv->flags & MLX4_EN_FLAG_ENABLE_HW_LOOPBACK) {
		struct ethhdr *ethh;

		/* Copy dst mac address to wqe. This allows loopback in eSwitch,
		 * so that VFs and PF can communicate with each other
		 */
		ethh = (struct ethhdr *)skb->data;
		tx_desc->ctrl.srcrb_flags16[0] = get_unaligned((__be16 *)ethh->h_dest);
		tx_desc->ctrl.imm = get_unaligned((__be32 *)(ethh->h_dest + 2));
	}

	/* Handle LSO (TSO) packets */
	if (lso_header_size) {
		int i;

		/* Mark opcode as LSO */
		op_own = cpu_to_be32(MLX4_OPCODE_LSO | (1 << 6)) |
			((ring->prod & ring->size) ?
				cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0);

		/* Fill in the LSO prefix */
		tx_desc->lso.mss_hdr_size = cpu_to_be32(
			shinfo->gso_size << 16 | lso_header_size);

		/* Copy headers;
		 * note that we already verified that it is linear */
		memcpy(tx_desc->lso.header, skb->data, lso_header_size);

		ring->tso_packets++;

		i = ((skb->len - lso_header_size) / shinfo->gso_size) +
			!!((skb->len - lso_header_size) % shinfo->gso_size);
		tx_info->nr_bytes = skb->len + (i - 1) * lso_header_size;
		ring->packets += i;
	} else {
		/* Normal (Non LSO) packet */
		op_own = cpu_to_be32(MLX4_OPCODE_SEND) |
			((ring->prod & ring->size) ?
			 cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0);
		tx_info->nr_bytes = MAX_T(unsigned int, skb->len, ETH_ZLEN);
		ring->packets++;
	}
	ring->bytes += tx_info->nr_bytes;
	netdev_tx_sent_queue(ring->tx_queue, tx_info->nr_bytes);
	AVG_PERF_COUNTER(priv->pstats.tx_pktsz_avg, skb->len);

	if (tx_info->inl)
		build_inline_wqe(tx_desc, skb, shinfo, real_size, &vlan_tag,
				 tx_ind, fragptr);

	if (skb->encapsulation) {
		struct iphdr *ipv4 = (struct iphdr *)skb_inner_network_header(skb);
		if (ipv4->protocol == IPPROTO_TCP || ipv4->protocol == IPPROTO_UDP)
			op_own |= cpu_to_be32(MLX4_WQE_CTRL_IIP | MLX4_WQE_CTRL_ILP);
		else
			op_own |= cpu_to_be32(MLX4_WQE_CTRL_IIP);
	}

	ring->prod += nr_txbb;

	/* If we used a bounce buffer then copy descriptor back into place */
	if (unlikely(bounce))
		tx_desc = mlx4_en_bounce_to_desc(priv, ring, index, desc_size);

	skb_tx_timestamp(skb);

	/* Check available TXBBs And 2K spare for prefetch */
	stop_queue = (int)(ring->prod - ring_cons) >
		      ring->size - HEADROOM - MAX_DESC_TXBBS;
	if (unlikely(stop_queue)) {
		netif_tx_stop_queue(ring->tx_queue);
		ring->queue_stopped++;
	}
	send_doorbell = !skb->xmit_more || netif_xmit_stopped(ring->tx_queue);

	real_size = (real_size / 16) & 0x3f;

	if (ring->bf_enabled && desc_size <= MAX_BF && !bounce &&
	    !skb_vlan_tag_present(skb) && send_doorbell) {
		tx_desc->ctrl.bf_qpn = ring->doorbell_qpn |
				       cpu_to_be32(real_size);

		op_own |= cpu_to_be32((bf_index & 0xffff) << 8);
		/* Ensure new descriptor hits memory
		 * before setting ownership of this descriptor to HW
		 */
		bus_wmb();
		tx_desc->ctrl.owner_opcode = op_own;

		wmb();

		mlx4_bf_copy(ring->bf.reg + ring->bf.offset, &tx_desc->ctrl,
			     desc_size);

		wmb();

		ring->bf.offset ^= ring->bf.buf_size;
	} else {
		tx_desc->ctrl.vlan_tag = cpu_to_be16(vlan_tag);
		tx_desc->ctrl.ins_vlan = MLX4_WQE_CTRL_INS_VLAN *
			!!skb_vlan_tag_present(skb);
		tx_desc->ctrl.fence_size = real_size;

		/* Ensure new descriptor hits memory
		 * before setting ownership of this descriptor to HW
		 */
		bus_wmb();
		tx_desc->ctrl.owner_opcode = op_own;
		if (send_doorbell) {
			wmb();
			/* Since there is no iowrite*_native() that writes the
			 * value as is, without byteswapping - using the one
			 * the doesn't do byteswapping in the relevant arch
			 * endianness.
			 */
#if defined(__LITTLE_ENDIAN)
			iowrite32(
#else
			iowrite32be(
#endif
				  ring->doorbell_qpn,
				  ring->bf.uar->map + MLX4_SEND_DOORBELL);
		} else {
			ring->xmit_more++;
		}
	}

	if (unlikely(stop_queue)) {
		/* If queue was emptied after the if (stop_queue) , and before
		 * the netif_tx_stop_queue() - need to wake the queue,
		 * or else it will remain stopped forever.
		 * Need a memory barrier to make sure ring->cons was not
		 * updated before queue was stopped.
		 */
		rmb();

		ring_cons = ACCESS_ONCE(ring->cons);
		if (unlikely(((int)(ring->prod - ring_cons)) <=
			     ring->size - HEADROOM - MAX_DESC_TXBBS)) {
			netif_tx_wake_queue(ring->tx_queue);
			ring->wake_queue++;
		}
	}
	return NETDEV_TX_OK;

tx_drop_unmap:
	en_err(priv, "DMA mapping error\n");

	while (++i_frag < shinfo->nr_frags) {
		++data;
		dma_unmap_page(ddev, (dma_addr_t) be64_to_cpu(data->addr),
			       be32_to_cpu(data->byte_count),
			       PCI_DMA_TODEVICE);
	}

tx_drop:
	dev_kfree_skb_any(skb);
	priv->stats.tx_dropped++;
	return NETDEV_TX_OK;
}
#endif

void __mlx4_xmit_poke(void *args)
{
	struct ether *edev = ((struct mlx4_poke_args*)args)->edev;
	struct mlx4_en_priv *priv = ((struct mlx4_poke_args*)args)->priv;
	struct mlx4_en_tx_ring *ring = ((struct mlx4_poke_args*)args)->ring;
	struct block *block;

	while (!mlx4_en_ring_is_full(ring)) {
		block = qget(edev->oq);
		if (!block)
			break;
		mlx4_send_packet(block, priv, ring);
	}
}

void mlx4_transmit(struct ether *edev)
{
	struct mlx4_en_priv *priv = netdev_priv(edev);
	struct mlx4_en_tx_ring *ring;
	struct mlx4_poke_args args;

	/* TODO multi-queue support. */
	ring = priv->tx_ring[0];
	args.edev = edev;
	args.priv = priv;
	args.ring = ring;
	poke(&ring->poker, &args);
}
