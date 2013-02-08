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

# mount the hdd image
modprobe loop max_part=10
losetup /dev/loop5 /home/brho/classes/ros/ros-kernel/mnt/hdd.img
sleep 5
mount /dev/loop5p1 /home/brho/classes/ros/ros-kernel/mnt/hdd
