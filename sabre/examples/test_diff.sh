#!/bin/bash

#
KERNEL=$1
ROOTFS=$2

./setup.sh &
sleep 1

./boot_microvm.sh ${KERNEL} ${ROOTFS}
sleep 3

./make_snaphot.sh Diff snapshot
sleep 3

./setup.sh &
sleep 1

./restore_snapshot.sh Diff snapshot
sleep 3

./setup.sh &
sleep 1

./boot_microvm.sh ${KERNEL} ${ROOTFS}
sleep 3

./make_snaphot.sh DiffCompressed snapshot
sleep 3

./setup.sh &
sleep 1

./restore_snapshot.sh DiffCompressed snapshot
sleep 3

sudo pkill -9 firecracker
