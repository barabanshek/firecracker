#!/usr/bin/env bash

cpu_freq=$1

# We need huge pages for the decompression buffer (although it's not required)
echo 0 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 0 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
echo 4000 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 4000 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

# Configure IAA
./configure_iaa_user.sh 0 1,15 16 8

# Fix CPU frequencies
./prepare_machine.sh ${cpu_freq}
