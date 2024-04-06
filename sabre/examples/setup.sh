#!/bin/bash

# Kill all running instanses of Firecracker.
sudo pkill -9 firecracker

#
ARCH="$(uname -m)"
API_SOCKET="/tmp/firecracker.socket"
sudo rm -f $API_SOCKET

# Start it.
sudo -E ./../../build/cargo_target/${ARCH}-unknown-linux-gnu/release/firecracker --api-sock "${API_SOCKET}"
