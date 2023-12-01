#!/bin/bash

sudo setfacl -m u:${USER}:rw /dev/kvm

FCH=`ps -ef | grep -oP "firecracker.sock --id \K.*" | wc -l`
if [ $FCH -gt 2 ]
then
    echo "Multiple VMs found"
    exit 1
fi

FIRECRACKER_HASH=`ps -ef | grep -oP "firecracker.sock --id \K.*" | head -n 1`

echo "Pausing VM #{${FIRECRACKER_HASH}}"
curl --unix-socket /var/lib/firecracker-containerd/shim-base/default#${FIRECRACKER_HASH}/firecracker.sock -i \
    -X PATCH 'http://localhost/vm' \
    -H 'Accept: application/json' \
    -H 'Content-Type: application/json' \
    -d '{
        "state": "Paused"
    }'

echo "Making snapshot of VM #{${FIRECRACKER_HASH}}"
curl --unix-socket /var/lib/firecracker-containerd/shim-base/default#${FIRECRACKER_HASH}/firecracker.sock -i \
    -X PUT 'http://localhost/snapshot/create' \
    -H  'Accept: application/json' \
    -H  'Content-Type: application/json' \
    -d '{
        "snapshot_type": "'$1'",
        "snapshot_path": "'$2'.snapshot.file",
        "mem_file_path": "'$2'.memory.file",
        "version": "1.1.0"
    }'
echo "      snaphot memory.file: /var/lib/firecracker-containerd/shim-base/default#${FIRECRACKER_HASH}/$2.memory.file"
SIZE=`ls -sh /var/lib/firecracker-containerd/shim-base/default#${FIRECRACKER_HASH}/$2.memory.file`
echo "      size: ${SIZE}"
echo ""

echo "Resuming VM #{${FIRECRACKER_HASH}}"
curl --unix-socket /var/lib/firecracker-containerd/shim-base/default#${FIRECRACKER_HASH}/firecracker.sock -i \
    -X PATCH 'http://localhost/vm' \
    -H 'Accept: application/json' \
    -H 'Content-Type: application/json' \
    -d '{
        "state": "Resumed"
    }'

# Process snapshot.
rm m.file; touch m.file
sudo ../../a.out /var/lib/firecracker-containerd/shim-base/default#${FIRECRACKER_HASH}/$2.memory.file m.file 2> /dev/null
sudo ../iaa_qpl_compress/build/compression_engine_demo --input_filename=m.file --logtostderr
