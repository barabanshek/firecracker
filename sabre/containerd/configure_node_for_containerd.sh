#!/usr/bin/env bash

ARCH="$(uname -m)"
FIRECRACKER_PATH=$(pwd)/../../build/cargo_target/${ARCH}-unknown-linux-gnu/release/firecracker

# Configure our firecracker-containerd binary to use our new snapshotter and
# separate storage from the default containerd binary.
sudo mkdir -p /etc/firecracker-containerd
sudo mkdir -p /var/lib/firecracker-containerd/containerd
# Create the shim base directory for which firecracker-containerd will run the
# shim from
sudo mkdir -p /var/lib/firecracker-containerd

# Configs.
sudo tee /etc/firecracker-containerd/config.toml <<EOF
version = 2
disabled_plugins = ["io.containerd.grpc.v1.cri"]
root = "/var/lib/firecracker-containerd/containerd"
state = "/run/firecracker-containerd"
[grpc]
  address = "/run/firecracker-containerd/containerd.sock"
[plugins]
  [plugins."io.containerd.snapshotter.v1.devmapper"]
    pool_name = "fc-dev-thinpool"
    base_image_size = "10GB"
    root_path = "/var/lib/firecracker-containerd/snapshotter/devmapper"

[debug]
  level = "debug"
EOF

# Setup device mapper thin pool.
sudo mkdir -p /var/lib/firecracker-containerd/snapshotter/devmapper
pushd /var/lib/firecracker-containerd/snapshotter/devmapper
DIR=/var/lib/firecracker-containerd/snapshotter/devmapper
POOL=fc-dev-thinpool

if [[ ! -f "${DIR}/data" ]]; then
    sudo touch "${DIR}/data"
    sudo truncate -s 100G "${DIR}/data"
fi

if [[ ! -f "${DIR}/metadata" ]]; then
    sudo touch "${DIR}/metadata"
    sudo truncate -s 2G "${DIR}/metadata"
fi

DATADEV="$(sudo losetup --output NAME --noheadings --associated ${DIR}/data)"
if [[ -z "${DATADEV}" ]]; then
    DATADEV="$(sudo losetup --find --show ${DIR}/data)"
fi

METADEV="$(sudo losetup --output NAME --noheadings --associated ${DIR}/metadata)"
if [[ -z "${METADEV}" ]]; then
    METADEV="$(sudo losetup --find --show ${DIR}/metadata)"
fi

SECTORSIZE=512
DATASIZE="$(sudo blockdev --getsize64 -q ${DATADEV})"
LENGTH_SECTORS=$(bc <<< "${DATASIZE}/${SECTORSIZE}")
DATA_BLOCK_SIZE=128
LOW_WATER_MARK=32768
THINP_TABLE="0 ${LENGTH_SECTORS} thin-pool ${METADEV} ${DATADEV} ${DATA_BLOCK_SIZE} ${LOW_WATER_MARK} 1 skip_block_zeroing"
echo "${THINP_TABLE}"

if ! $(sudo dmsetup reload "${POOL}" --table "${THINP_TABLE}"); then
    sudo dmsetup create "${POOL}" --table "${THINP_TABLE}"
fi

popd

# Configure the aws.firecracker runtime.
sudo mkdir -p /var/lib/firecracker-containerd/runtime
sudo cp firecracker-containerd/tools/image-builder/rootfs.img /var/lib/firecracker-containerd/runtime/default-rootfs.img
sudo cp hello-vmlinux.bin /var/lib/firecracker-containerd/runtime/default-vmlinux.bin
sudo mkdir -p /etc/containerd

# Configs.
sudo tee /etc/containerd/firecracker-runtime.json <<EOF
{
  "firecracker_binary_path": "${FIRECRACKER_PATH}",
  "cpu_template": "T2",
  "log_fifo": "fc-logs.fifo",
  "log_levels": ["debug"],
  "metrics_fifo": "fc-metrics.fifo",
  "kernel_args": "console=ttyS0 noapic reboot=k panic=1 pci=off nomodules ro systemd.unified_cgroup_hierarchy=0 systemd.journald.forward_to_console systemd.unit=firecracker.target init=/sbin/overlay-init",
  "default_network_interfaces": [{
    "CNIConfig": {
      "NetworkName": "fcnet",
      "InterfaceName": "veth0"
    }
  }]
}
EOF

# CNI.
sudo cp firecracker-containerd/tools/demo/fcnet.conflist  /etc/cni/conf.d/fcnet.conflist

echo "Node is fully configured and ready!"
