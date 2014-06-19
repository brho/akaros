ash root
cp /mnt/go.cpio .
cpio -d -i < go.cpio
listen1 tcp!*!23 /bin/ash

