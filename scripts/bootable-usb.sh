#!/bin/bash
echo "you need to customize this script; don't run it without editing"
USBDRIVE=/dev/sdwhatever
MNTPOINT=/mnt/wherever
# rootdir contains the files from an existing image.  minimum of /extlinux.conf,
# /boot/, and /boot/mboot.c32
ROOTDIR=/path/to/rootdir/files/
USER=username
GROUP=usergroup

# comment this once you're done
exit

echo "make one partition, bootable and type 83 (linux)"

fdisk $USBDRIVE
mke2fs ${USBDRIVE}1
mount ${USBDRIVE}1 $MNTPOINT
# copy in the contents of the rootfs.  extlinux.conf in the main directory.  no
# ldlinux (extlinux will add it later).  we put all the images in /boot.
cp -r $ROOTDIR/* $MNTPOINT
chown -R $USER:$GROUP $MNTPOINT
extlinux -i $MNTPOINT
umount $MNTPOINT
# this mbr is the same as extlinux's
dd if=$ROOTDIR/mbr.bin of=${USBDRIVE}

# other notes:
######################
# over a serial connection, you'll only see:
#    Booting from Hard Disk...
#    Booting from 0000:7c00
#   on a monitor, you'll see the boot: prompt

# put something like this in your Akaros Makelocal:
#$(OBJDIR)/kern/.usb.touch: $(KERNEL_OBJ)
#	@echo "  (USB) Copying to /dev/sdb1"
#	$(Q)mount /dev/sdb1
#	$(Q)sudo cp $^ /mnt/wherever/boot/akaros
#	@sync
#	$(Q)umount /mnt/wherever
#	@touch $@
#
#usb: $(OBJDIR)/kern/.usb.touch ;

# here's a basic extlinux.conf
#PROMPT 1
#TIMEOUT 50
#
#DEFAULT akaros
#
#LABEL akaros
#    MENU LABEL Akaros
#    MENU DEFAULT
#    KERNEL /boot/mboot.c32
#    APPEND /boot/akaros
