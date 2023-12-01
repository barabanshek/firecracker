NAME=hello-rootfs.ext4
SIZE=500

#
rm ${NAME}
touch ${NAME}

#
dd if=/dev/zero of=${NAME} bs=1M count=${SIZE}
mkfs.ext4 ${NAME}
sudo mount ${NAME} /mnt/fire

#
docker run -it --rm -v /mnt/fire:/my-rootfs alpine

# apk add openrc
# apk add util-linux
# passwd root
# paste script from here: https://github.com/firecracker-microvm/firecracker/blob/main/docs/rootfs-and-kernel-setup.md

#
sudo umount ${NAME}
