#!/usr/bin/env bash

GO_VERSION='1.21'

#
# Deps.
#
sudo mkdir -p /etc/apt/sources.list.d
echo "deb http://ftp.debian.org/debian bullseye-backports main" | \
  sudo tee /etc/apt/sources.list.d/bullseye-backports.list
sudo DEBIAN_FRONTEND=noninteractive apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get \
  install --yes \
  golang-${GO_VERSION} \
  make \
  git \
  curl \
  e2fsprogs \
  util-linux \
  bc \
  gnupg

export PATH=/usr/lib/go-${GO_VERSION}/bin:$PATH

# Install device-mapper.
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y dmsetup

#
# Firecracker-containerd.
#
rm -rf firecracker-containerd
git clone https://github.com/firecracker-microvm/firecracker-containerd.git
pushd firecracker-containerd

# Apply patch to use more recent go and firecracker go-sdk.
git apply ../go.mod.patch
go mod tidy
pushd examples/cmd/remote-snapshotter
go mod tidy
popd

# Make agent.
make agent-in-docker

# Make it.
make all

# For networking support.
make demo-network

# Make rootfs image.
make image

# Download kernel
curl -fsSL -o hello-vmlinux.bin https://s3.amazonaws.com/spec.ccfc.min/img/quickstart_guide/x86_64/kernels/vmlinux.bin

echo "firecracker-containerd is installed with all dependencies"
popd
