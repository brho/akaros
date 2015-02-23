#ifndef ROS_KERN_COMPAT_TODO_H
#define ROS_KERN_COMPAT_TODO_H

struct mdio_if_info {			// need to interface with mii stuff?
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

typedef int netdev_tx_t;
typedef int16_t __sum16;
typedef uint16_t __le;
typedef uint8_t __u8;
typedef int select_queue_fallback_t;
enum pkt_hash_types {
	Two,
};

#endif /* ROS_KERN_COMPAT_TODO_H */
