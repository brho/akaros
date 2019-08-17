Intel CBDMA
===========
**2019-08-16** Aditya Basu (`mitthu`)

Contents
-------------------------------
+ About the driver
+ Example Interaction via sysfs

About the driver
-------------------------------
* Only one CBDMA function gets registered by the driver.
* Also the driver only works with CBDMA devices on bus 0. This is to avoid
  dealing with scoped DRHDs when performing IOMMU passthru.

Example Interaction via sysfs
-------------------------------
```bash
# Mount the device
bash-4.3$ mkdir -p /sys/cbdma
bash-4.3$ /bin/bind \#cbdma /sys/cbdma
bash-4.3$ cd /sys/cbdma/

# All files
bash-4.3$ ls
iommu  ktest  reset  stats  ucopy

# Display information about CBDMA
bash-4.3$ cat stats
Intel CBDMA [8086:2021] registered at 00:03.0
    Driver Information:
        mmio: 0xfffffff0000a0000
        mmio_phy: 0xfebf0000
        mmio_sz: 16384
        total_channels: 1
        desc_kaddr: 0xffff8001104c2000
        desc_paddr: 0x00000001104c2000
        desc_num: 1
        ver: 0x33
        status_kaddr: 0xffff80010c8d5960
        status_paddr: 0x000000010c8d5960
        status_value: 0x91c1420
    PCIe Config Registers:
        PCICMD: 0x406
        PCISTS: 0x10
        RID: 0x4
        CB_BAR: 0xfebf0004
        DEVSTS: 0x0
        PMCSR: 0x8
        DMAUNCERRSTS: 0x0
        DMAUNCERRMSK: 0x0
        DMAUNCERRSEV: 0x98
        DMAUNCERRPTR: 0xa
        DMAGLBERRPTR: 0x0
        CHANERR_INT: 0x0
        CHANERRMSK_INT: 0x60000
        CHANERRSEV_INT: 0x10000
        CHANERRPTR: 0x2
    CHANNEL_0 MMIO Registers:
        CHANCMD: 0x0
        CBVER: 0x33 major=3 minor=3
        CHANCTRL: 0x10c
        CHANSTS: 0x3 [HALTED], desc_addr: 0x0000000000000000, raw: 0x3
        CHAINADDR: 0x0000000000000000
        CHANCMP: 0x0000000000000000
        DMACOUNT: 0
        CHANERR: 0x0

# Perform self-test (multiple runs)
bash-4.3$ cat ktest
cbdma: info: DMACOUNT = 1
Self-test Intel CBDMA [8086:2021] registered at 00:03.0
        Channel Status: ACTIVE (raw: 0x0)
        Copy Size: 64 (0x40)
        srcfill: 2 (0x32)
        dstfill: 0 (0x30)
        src_str (after copy): 111111111111111111111111111111111111111111111111111111111111111
        dst_str (after copy): 111111111111111111111111111111111111111111111111111111111111111

bash-4.3$ cat ktest
cbdma: info: DMACOUNT = 1
Self-test Intel CBDMA [8086:2021] registered at 00:03.0
        Channel Status: ACTIVE (raw: 0x0)
        Copy Size: 64 (0x40)
        srcfill: 3 (0x33)
        dstfill: 0 (0x30)
        src_str (after copy): 222222222222222222222222222222222222222222222222222222222222222
        dst_str (after copy): 222222222222222222222222222222222222222222222222222222222222222

# Reset the CBDMA
bash-4.3$ echo 1 >reset
cbdma: reset performed

bash-4.3$ cat reset
Status: No pending reset
Write '1' to perform reset!

# Performing DMA with the user-space companion program (with IOMMU in front)
# Note: This will not work with QEMU! Akaros should be running bare-metal.
bash-4.3$ echo 1 >iommu
bash-4.3$ cat iommu
IOMMU enabled = yes
Write '0' to disable or '1' to enable the IOMMU

bash-4.3$ ucbdma 00:03.0
got device: 00:04.0
Mappings for iommu@0xffff80007bebfce8
    <empty>
Mappings for iommu@0xffff800009376ae8
    pid = 72
        device = 0:3.0
[user] page size: 4096 bytes
[user] src: 1111111111111111111
[user] dst: 0000000000000000000
[user] ucbdma: 0x100000000, size: 80 (or 0x50)
[user]  desc->xref_size: 20
[user]  desc->src_addr: 0x1000000b4
[user]  desc->dest_addr: 0x1000000c8
[user]  desc->next_desc_addr: 0x100000000
[user]  ndesc: 1
[user]  status: 0x0
[user] ucbdma ptr: 0x100000000
[kern] value from userspace: 0x0000000100000000
[kern] IOMMU = ON
[kern] ucbdma: user: 0x0000000100000000 kern: 0xffff8002929a4000 ndesc: 1
cbdma: info: DMACOUNT = 1
[user] ucbdma: 0x100000000, size: 80 (or 0x50)
[user]  desc->xref_size: 20
[user]  desc->src_addr: 0x1000000b4
[user]  desc->dest_addr: 0x1000000c8
[user]  desc->next_desc_addr: 0x100000000
[user]  ndesc: 1
[user]  status: 0x1b041003
[user] channel_status: 1b041003
[user] src: 1111111111111111111
[user] dst: 0000000000000000000
```
