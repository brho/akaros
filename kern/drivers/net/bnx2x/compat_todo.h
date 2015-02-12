#ifndef ROS_KERN_COMPAT_TODO_H
#define ROS_KERN_COMPAT_TODO_H

struct mdio_if_info {			// need to interface with mii stuff?
};
struct sk_buff {		// block
};
struct skb_shared_info {
};
struct ethhdr {
};
struct vlan_ethhdr {
};
struct napi_struct {	// rewrite stuff
};
struct napi_gro_cb {
};
struct timer_list {		// alarms
};
struct ifla_vf_info {
};
struct ethtool_cmd {
};
struct ifreq {
};
struct tcphdr {
};
struct iphdr {
};
struct ipv6hdr {
};
struct ethtool_channels {
};
struct ethtool_coalesce {
};
struct ethtool_drvinfo {
};
struct ethtool_dump {
};
struct ethtool_eee {
};
struct ethtool_eeprom {
};
struct ethtool_modinfo {
};
struct ethtool_pauseparam {
};
struct ethtool_regs {
};
struct ethtool_ringparam {
};
struct ethtool_rxnfc {
};
struct ethtool_stats {
};
struct ethtool_test {
};
struct ethtool_ts_info {
};
struct ethtool_wolinfo {
};
struct netdev_phys_item_id {
};

typedef int netdev_tx_t;
typedef int16_t __sum16;
typedef uint16_t __le;
typedef uint8_t __u8;
typedef int select_queue_fallback_t;
enum ethtool_phys_id_state {
	One,
};
enum pkt_hash_types {
	Two,
};

#endif /* ROS_KERN_COMPAT_TODO_H */
