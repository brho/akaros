#pragma once

#ifndef ROS_KERN_LINUX_COMPAT_H
#error "Do not include compat_todo.h directly"
#endif

/* These should be moved eventually */

/* Plan 9 could use this as a helper */
static inline bool is_multicast_ether_addr(uint8_t *mac)
{
	return mac[0] & 1;
}

/* We have this in devether, probably should expose it */
static inline int eaddrcmp(uint8_t *x, uint8_t *y)
{
	uint16_t *a = (uint16_t *)x;
	uint16_t *b = (uint16_t *)y;

	return (a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2]);
}



struct mdio_if_info {		// need to interface with mii stuff?
};
struct sk_buff {		// block
};
struct skb_shared_info {
};
struct napi_struct {	// rewrite stuff
};
struct napi_gro_cb {
};
struct ifla_vf_info {
};
struct ifreq {
};
struct netdev_phys_item_id {
};

typedef int16_t __sum16;
typedef uint16_t __le;
typedef uint8_t __u8;
typedef int select_queue_fallback_t;
enum pkt_hash_types {
	Two,
};
