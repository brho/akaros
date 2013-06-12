#!/bin/bash
# example of brho's kvm-up.sh, which I run every time I boot my dev machine.
# you'll need to change paths, usernames, and other things for your machine.

# set up networking.  feel free to comment this out.
modprobe tun
brctl addbr br0
tunctl -u brho -t tap0
ifconfig tap0 0.0.0.0 up
brctl addif br0 tap0
sleep 2
/etc/init.d/net.br0 start
/etc/init.d/dnsmasq start

# set up some variables
MNTDIR=/home/brho/classes/ros/ros-kernel/mnt
MNTPOINT=$MNTDIR/hdd
HDDIMG=$MNTDIR/hdd.img

# mount the hdd image
modprobe loop max_part=10
losetup /dev/loop5 $HDDIMG
sleep 5
mount -o sync /dev/loop5p1 $MNTPOINT
chown -R brho:brho $MNTPOINT

## Alternative method if you have the loopback built into the kernel
## mount the hdd image with a hardcoded offset, specific to the image we
## provide
#losetup /dev/loop5 $HDDIMG
#losetup -o 1048576 /dev/loop6 /dev/loop5
#mount -o sync /dev/loop6 $MNTPOINT
#chown -R brho:brho $MNTPOINT

