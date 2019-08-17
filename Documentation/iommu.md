I/O MMU
=======
**2019-08-15** Aditya Basu (`mitthu`)

Contents
-------------------------------
+ Acronyms
+ I/O MMU Workings
    - IOTLB shootdowns
+ Running Akaros in QEMU
+ Example Interaction via sysfs

Acronyms
-------------------------------
+ IEC:  Interrupt Entry Cache
+ RWBF: Required Write-Buffer Flushing
+ IVT:  Invalidate IOTLB

I/O MMU Workings
-------------------------------

### IOTLB Shootdowns / flushing

* If RWBF set in capability, then perform write buffer flushing.
* If IOTLB is present, then we can perform one of the following:
    + global shootdown
    + DID specific shootdown (We perform this!)
    + page-specific shootdown for a specific DID

Running Akaors in QEMU
-------------------------------
* Currently running in QEMU requires a recompilation. This is to allow 4-level
  paging for the IOMMU. Do the following:
    - For qemu version 4.0.50, we need to modify line 52 of
      "include/hw/i386/intel_iommu.h".
    - The macro `VTD_HOST_ADDRESS_WIDTH` will be `VTD_HOST_AW_39BIT`. Change it
      to `VTD_HOST_AW_48BIT` and recompile.

```bash
# Prepare device for passthrough (on Linux host)
# - unbind from existing driver
# - make sure the VFIO module is loaded
# - make sure all virtual functions are released by the driver
# - bind to VFIO driver for passthrough
$ PCIDEVICE_BDF=0000:00:04.*
$ echo $(PCIDEVICE_BDF) | sudo tee /sys/bus/pci/devices/$(PCIDEVICE_BDF)/driver/unbind
$ sudo modprobe vfio-pci
$ sudo rmmod ioatdma
$ echo $(PCIDEVICE_ID) | sudo tee /sys/bus/pci/drivers/vfio-pci/new_id

# Standard run
$ sudo $(QEMU) \
    -enable-kvm \
    -cpu host \
    -smp 8 \
    -m 4096 \
    -nographic \
    -net nic,model=e1000 \
    -net user,hostfwd=tcp::5555-:22 \
    -machine q35,accel=kvm,kernel-irqchip=split \
    -device intel-iommu,intremap=off,caching-mode=on,device-iotlb=on \
    -device vfio-pci,host=00:04.0 \
    -kernel obj/kern/akaros-kernel

# Trace QEMU calls (for debugging)
$ echo -n '' >/tmp/qemu-trace
$ echo 'vfio_pci_read_config' >>/tmp/qemu-trace
$ echo 'vfio_pci_write_config' >>/tmp/qemu-trace
$ echo 'vfio_region_read' >>/tmp/qemu-trace
$ echo 'vfio_region_write' >>/tmp/qemu-trace
$ echo 'pci_data_read' >>/tmp/qemu-trace

$ sudo $(QEMU) \
    -trace events=/tmp/qemu-trace \
    -enable-kvm \
    -cpu host \
    -smp 8 \
    -m 4096 \
    -nographic \
    -net nic,model=e1000 \
    -net user,hostfwd=tcp::5555-:22 \
    -device vfio-pci,host=00:04.0 \
    -kernel obj/kern/akaros-kernel
```

Example Interaction via sysfs
-------------------------------
```bash
# Mount the device
bash-4.3$ mkdir -p /sys/iommu
bash-4.3$ /bin/bind \#iommu /sys/iommu
bash-4.3$ cd /sys/iommu/

# All files
bash-4.3$ ls
attach    detach    info      mappings  power

# Attach devices
bash-4.3$ echo 0:3.0 1 >attach
bash-4.3$ echo 0:0.0 22 >attach
bash-4.3$ echo 0:1:0 22 >attach
bash-4.3$ echo 0:2.0 26 >attach

# Display mappings
bash-4.3$ cat mappings
Mappings for iommu@0xffff8001090acce8
        pid = 1
                device = 0:3.0
        pid = 22
                device = 0:0.0
                device = 0:1.0
        pid = 26
                device = 0:2.0

# Detach devices
bash-4.3$ echo 0:2.0 >detach
bash-4.3$ echo 0:1:0 >detach
bash-4.3$ cat mappings
Mappings for iommu@0xffff8001090acce8
        pid = 1
                device = 0:3.0
        pid = 22
                device = 0:0.0

# Display IOMMU information
bash-4.3$ cat info
driver info:
        default did = 1
        status = enabled

iommu@0xffff8001090ab0e8
        rba = 0x00000000fed90000
        supported = yes
        num_assigned_devs = 0
        regspace = 0xfffffff000000000
        host addr width (dmar) = 48
        host addr width (cap[mgaw]) = 48
        version = 0x10
        capabilities = 0x00d2008c222f0686
                mgaw: 48
                sagaw (paging level): 0x6
                caching mode: yes (1)
                zlr: 0x0
                rwbf: not required
                num domains: 65536
                supports protected high-memory region: no
                supports Protected low-memory region: no
        ext. capabilities = 0x0000000000000f46
                pass through: yes
                device iotlb: yes
                iotlb register offset: 0xf0
                snoop control: no
                coherency: no
                queue invalidation support: yes
                interrupt remapping support: no
                extended interrupt mode: 0x0
        global status = 0xc0000000
                translation: enabled
                root table: set
        root entry table = 0x00000001090aa000 (phy) or 0xffff8001090aa000 (vir)

# Turn off IOMMU (global)
bash-4.3$ cat power
IOMMU status: enabled
Write 'enable' or 'disable' OR 'on' or 'off' to change status

bash-4.3$ echo -n off >power

bash-4.3$ cat power
IOMMU status: disabled
Write 'enable' or 'disable' OR 'on' or 'off' to change status
```
