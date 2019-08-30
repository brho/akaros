@@
typedef u8;
typedef uint8_t;
@@
-u8
+uint8_t

@@
typedef u16;
typedef uint16_t;
@@
-u16
+uint16_t

@@
typedef __u16;
@@
-__u16
+uint16_t

@@
typedef u32;
typedef uint32_t;
@@
-u32
+uint32_t

@@
typedef u64;
typedef uint64_t;
@@
-u64
+uint64_t

@@
typedef cycle_t;
typedef uint64_t;
@@
-cycle_t
+uint64_t

@@
typedef s8;
typedef int8_t;
@@
-s8
+int8_t

@@
typedef s16;
typedef int16_t;
@@
-s16
+int16_t

@@
typedef s32;
typedef int32_t;
@@
-s32
+int32_t

@@
typedef s64;
typedef int64_t;
@@
-s64
+int64_t

@@
typedef uint;
@@
-uint
+unsigned int

@@
typedef __sum16;
typedef uint16_t;
@@
-__sum16
+uint16_t

@@
typedef __wsum;
typedef uint32_t;
@@
-__wsum
+uint32_t

@@
@@
-ETH_ALEN
+Eaddrlen

@@
@@
-ETH_HLEN
+ETHERHDRSIZE

@@
@@
-ETH_DATA_LEN
+ETHERMAXTU

@@
@@
 struct
-pci_dev
+pci_device

@@
@@
 struct
-net_device
+ether

// Akaros's netif_stats is Linux's rtnl_link_stats64, which is a superset (byte
// for byte) of net_device_stats.
@@
@@
 struct
-rtnl_link_stats64
+netif_stats

@@
@@
 struct
-net_device_stats
+netif_stats

@@
struct pci_device *p;
@@
-p->irq
+p->irqline

@@
struct pci_device *p;
@@
-p->subsystem_vendor
+pci_get_subvendor(p)

@@
struct pci_device *p;
@@
-p->subsystem_device
+pci_get_subdevice(p)

@@
struct pci_device *p;
@@
-p->dev
+p->linux_dev

@@
struct pci_device *p;
@@
-p->device
+p->dev_id

@@
struct pci_device *p;
@@
-p->vendor
+p->ven_id

@@
struct ether *p;
@@
-p->features
+p->feat

@@
struct ether *p;
expression E;
@@
-p->vlan_features = E;

@@
struct ether *p;
expression E;
@@
-p->hw_enc_features = E;

@@
struct ether *p;
@@
-p->dev_addr
+p->ea

@@
struct ether *p;
expression E;
@@
-p->dev_port = E;

@@
struct ether *p;
expression E;
@@
-p->addr_len = E;

@@
struct ether *p;
@@
-p->addr_len
+Eaddrlen

@@
struct ether *p;
expression E;
@@
-p->netdev_ops = E;

@@
struct ether *p;
expression E;
@@
-p->watchdog_timeo = E;

@@
struct ether *p;
expression E;
@@
-p->ethtool_ops = E;
