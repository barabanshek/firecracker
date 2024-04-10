#!/usr/bin/env bash

sudo rm -fr /etc/firecracker-containerd
sudo rm -fr /var/lib/firecracker-containerd/containerd
sudo rm -fr /var/lib/firecracker-containerd
sudo rm -fr /var/lib/firecracker-containerd/snapshotter/devmapper
sudo rm -fr /var/lib/firecracker-containerd/runtime
sudo rm -fr /etc/containerd

sudo dmsetup remove_all -f
