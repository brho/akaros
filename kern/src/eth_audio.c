/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Rimas's Ethernet-Audio device */

#ifdef __CONFIG_ETH_AUDIO__

#include <eth_audio.h>
#include <string.h>
#include <devfs.h>
#include <page_alloc.h>
#include <pmap.h>
#include <arch/nic_common.h>
#include <process.h>
#include <smp.h>

struct file_operations ethaud_in_f_op;
struct file_operations ethaud_out_f_op;
struct page_map_operations ethaud_pm_op;

/* There is only one packet we'll ever send out.  It's a full ethernet frame
 * that we build and submit to send_frame() (which does another memcpy). */
struct ethaud_udp_packet eth_udp_out;

/* Consider a lock to protect this */
struct proc *active_proc = 0;

/* Nodes/inodes representing the device */
struct inode *ethaud_in, *ethaud_out;

/* Builds the device nodes in /dev */
void eth_audio_init(void)
{
	struct page *page;
	struct file *in_f, *out_f;
	in_f = make_device("/dev/eth_audio_in", S_IRUSR, __S_IFBLK,
	                   &ethaud_in_f_op);
	out_f = make_device("/dev/eth_audio_out", S_IRUSR | S_IWUSR, __S_IFBLK,
	                    &ethaud_out_f_op);
	/* Grab and save the inodes (might change if we change make_device) */
	ethaud_in = in_f->f_dentry->d_inode;
	kref_get(&ethaud_in->i_kref, 1);
	ethaud_out = out_f->f_dentry->d_inode;
	kref_get(&ethaud_out->i_kref, 1);
	/* Let go of the files (mostly used to get the dentry in the right place) */
	kref_put(&in_f->f_kref);
	kref_put(&out_f->f_kref);
	/* make sure the inode tracks the right pm op */
	ethaud_in->i_mapping->pm_op = &ethaud_pm_op;
	ethaud_out->i_mapping->pm_op = &ethaud_pm_op;
	/* zalloc pages and associate them with the devices' inodes */
	assert(!kpage_alloc(&page));
	memset(page2kva(page), 0, PGSIZE);
	ethaud_in->i_fs_info = page;
	assert(!kpage_alloc(&page));
	memset(page2kva(page), 0, PGSIZE);
	ethaud_out->i_fs_info = page;
}

/* Lots of unnecessary copies.  For now, we need to build the ethernet frame,
 * which means we prepend the ethernet header... */
static void eth_audio_sendpacket(void *buf)
{
	int retval;
	/* Fill the outgoing buffer (Copy #1.  2 is in send_frame, 3 is the DMA). */
	memcpy(&eth_udp_out.payload, buf, ETH_AUDIO_PAYLOAD_SZ);
	/* Make sure there is still a reasonable header. */
	static_assert(sizeof(eth_udp_out) == ETH_AUDIO_FRAME_SZ);
	/* Should compute the UDP checksum before sending the frame out.  The
	 * Eth-audio device shouldn't care (and Linux seems to be okay with packets
	 * that have no checksum (but not a wrong checksum)).  Technically, this
	 * hurts our performance a bit (and some NICs can offload this). */
	eth_udp_out.udp_hdr.checksum = htons(udp_checksum(&eth_udp_out.ip_hdr,
	                                                  &eth_udp_out.udp_hdr));
	/* Send it out */
	retval = send_frame((const char*)&eth_udp_out, ETH_AUDIO_FRAME_SZ);
	assert(retval >= 0);
}

/* This is how we know who to send the packet back to, since we have no real
 * networking stack.  Lots of assumptions about how things stay in sync. */
static void eth_audio_prep_response(struct ethaud_udp_packet *incoming,
                                    struct ethaud_udp_packet *outgoing)
{
	/* If you're looking for optimizations, we can do this just once */
	memcpy(&outgoing->eth_hdr.dst_mac, &incoming->eth_hdr.src_mac, 6); 
	memcpy(&outgoing->eth_hdr.src_mac, device_mac, 6); 
	outgoing->eth_hdr.eth_type = htons(IP_ETH_TYPE);
	outgoing->ip_hdr.version = IPPROTO_IPV4;
	outgoing->ip_hdr.hdr_len = ETH_AUDIO_IP_HDR_SZ >> 2;
	outgoing->ip_hdr.tos = 0;
	outgoing->ip_hdr.packet_len = htons(ETH_AUDIO_PAYLOAD_SZ + UDP_HDR_SZ +
	                                    ETH_AUDIO_IP_HDR_SZ);
	outgoing->ip_hdr.id = htons(0);
	outgoing->ip_hdr.flags_frags = htons(0);
	outgoing->ip_hdr.ttl = DEFAULT_TTL;
	outgoing->ip_hdr.protocol = IPPROTO_UDP;
	/* Need a non-broadcast IP.  Picking one higher than the sender's */
	outgoing->ip_hdr.src_addr = htonl(ntohl(incoming->ip_hdr.src_addr) + 1);
	outgoing->ip_hdr.dst_addr = incoming->ip_hdr.src_addr;
	/* Since the IP header is set already, we can compute the checksum. */
	outgoing->ip_hdr.checksum = htons(ip_checksum(&outgoing->ip_hdr));
	outgoing->udp_hdr.src_port = htons(ETH_AUDIO_SRC_PORT);
	outgoing->udp_hdr.dst_port = htons(ETH_AUDIO_DST_PORT);
	outgoing->udp_hdr.length = htons(ETH_AUDIO_PAYLOAD_SZ + UDP_HDR_SZ);
	outgoing->udp_hdr.checksum = htons(0);

	/* Debugging */
	static int once = 0;
	if (!once++)
		printd("I will send %d bytes from %08p:%d to %08p:%d, iplen %04p, "
		       "udplen %04p\n",
		       ntohs(outgoing->udp_hdr.length),
		       ntohl(outgoing->ip_hdr.src_addr),
		       ntohs(outgoing->udp_hdr.src_port), 
		       ntohl(outgoing->ip_hdr.dst_addr),
		       ntohs(outgoing->udp_hdr.dst_port),
		       ntohs(outgoing->ip_hdr.packet_len),
		       ntohs(outgoing->udp_hdr.length)
			   ); 
}

/* This is called by net subsys when it detects an ethernet audio packet.  Make
 * sure the in device has the contents, and send out the old frame. */
void eth_audio_newpacket(void *buf)
{
	struct page *in_page, *out_page;
	/* Bail out if these two haven't been initialized yet */
	if (!ethaud_in || !ethaud_out)
		return;
	/* Put info from the packet into the outgoing packet */
	eth_audio_prep_response((struct ethaud_udp_packet*)buf, &eth_udp_out);
	in_page = (struct page*)ethaud_in->i_fs_info;
	out_page = (struct page*)ethaud_out->i_fs_info;
	/* third copy (1st being the NIC to RAM). */
	memcpy(page2kva(in_page), buf + ETH_AUDIO_HEADER_OFF, ETH_AUDIO_PAYLOAD_SZ);
	/* Send the current outbound packet (can consider doing this by fsync) */
	eth_audio_sendpacket(page2kva(out_page));
	if (active_proc)
		proc_notify(active_proc, 0);
}

/* mmap() calls this to do any FS specific mmap() work.  Since our files are
 * defined to be only one page, we need to make sure they aren't mmapping too
 * much, and that it isn't a PRIVATE mapping.
 *
 * Then we need to make sure the one page of the VMR is mapped in the page table
 * (circumventing handle_page_fault and the page cache).  Avoiding the page
 * cache means we won't need to worry about accidentally unmapping this under
 * pressure and having to remap it (which will cause a readpage). */
int eth_audio_mmap(struct file *file, struct vm_region *vmr)
{
	struct page *page = (struct page*)file->f_dentry->d_inode->i_fs_info;
	/* Only allow mmaping from the start of the file */
	if (vmr->vm_foff)
		return -1;
	/* Only allow mmaping of a page */
	if (vmr->vm_end - vmr->vm_base != PGSIZE)
		return -1;
	/* No private mappings (would be ignored anyway) */
	if (vmr->vm_flags & MAP_PRIVATE)
		return -1;
	/* Only one proc can use it at a time (we need to know who to notify, since
	 * we don't have any sockets or other abstractions to work with).  Note we
	 * are storing a reference. */
	if (active_proc && active_proc != vmr->vm_proc)
		return -1;
	if (!active_proc) {
		kref_get(&vmr->vm_proc->p_kref, 1);
		active_proc = vmr->vm_proc;
	}
	assert(page);
	/* Get the PTE for the page this VMR represents */
	pte_t *pte = pgdir_walk(vmr->vm_proc->env_pgdir, (void*)vmr->vm_base, 1);
	if (!pte)
		return -ENOMEM;
	/* If there was a page there, it should have be munmapp()d. */
	assert(!PAGE_PRESENT(*pte));
	/* update the page table */
	int pte_prot = (vmr->vm_prot & PROT_WRITE) ? PTE_USER_RW :
	               (vmr->vm_prot & (PROT_READ|PROT_EXEC)) ? PTE_USER_RO : 0;
	/* Storing a reference to the page in the PTE */
	page_incref(page);
	*pte = PTE(page2ppn(page), PTE_P | pte_prot);
	return 0;
}

/* This shouldn't be called.  It could be if we ever handled a PF on the device,
 * and the device wasn't present in the page table.  This should not happen,
 * since ea_mmap() should have sorted that out.  Even if there was an unmap()
 * then a new mmap(), it still shouldn't happen. */
int eth_audio_readpage(struct page_map *pm, struct page *page)
{
	warn("Eth audio readpage!  (Did you page fault?)");
	return -1;
}

/* Called when the file is about to be closed (file obj freed).  This is the
 * least intrusive way to find out when the device is unmapped and closed, so we
 * can let go of the proc.  TODO: Note that currently, the kernel will not be
 * able to unmap the file by itself, due to the circular dependence of the kref,
 * proc_free, and destroy_vmr.  Programs have to unmap it themselves.  Since
 * this device is a bit of a hack anyway, I'm okay with not making big changes
 * to the kernel to support this yet. */
int eth_audio_release(struct inode *inode, struct file *file)
{
	/* Disconnect the proc from the device, decref. */
	if (active_proc && current) {
		assert(active_proc == current);
		kref_put(&active_proc->p_kref);
		active_proc = 0;
	}
	return 0;
}

/* File operations */
struct file_operations ethaud_in_f_op = {
	dev_c_llseek,	/* Errors out, can't llseek */
	0,				/* Can't read, only mmap */
	0,				/* Can't write, only mmap */
	kfs_readdir,	/* this will fail gracefully */
	eth_audio_mmap,
	kfs_open,
	kfs_flush,
	eth_audio_release,
	0,				/* fsync - makes no sense */
	kfs_poll,
	0,	/* readv */
	0,	/* writev */
	kfs_sendpage,
	kfs_check_flags,
};

struct file_operations ethaud_out_f_op = {
	dev_c_llseek,	/* Errors out, can't llseek */
	0,				/* Can't read, only mmap */
	0,				/* Can't write, only mmap */
	kfs_readdir,	/* this will fail gracefully */
	eth_audio_mmap,
	kfs_open,
	kfs_flush,
	eth_audio_release,
	0,				/* fsync - TODO: make this send the packet */
	kfs_poll,
	0,	/* readv */
	0,	/* writev */
	kfs_sendpage,
	kfs_check_flags,
};

/* Eth audio page map ops: */
struct page_map_operations ethaud_pm_op = {
	eth_audio_readpage,
};

#endif /* __CONFIG_ETH_AUDIO__ */
