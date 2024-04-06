#!/bin/bash

#
API_SOCKET="/tmp/firecracker.socket"

#
sudo setfacl -m u:${USER}:rw /dev/kvm

# Check for multiple VMs.
FCH=`ps -ef | grep -oP "firecracker.sock --id \K.*" | wc -l`
if [ $FCH -gt 2 ]
then
    echo "Multiple VMs found"
    exit 1
fi

FIRECRACKER_HASH=`ps -ef | grep firecracker.sock | awk '{print $2}' | head -n 1`

echo "Pausing VM #{${FIRECRACKER_HASH}}"
curl --unix-socket "${API_SOCKET}" -i \
    -X PATCH 'http://localhost/vm' \
    -H 'Accept: application/json' \
    -H 'Content-Type: application/json' \
    -d '{
        "state": "Paused"
    }'

echo "Making snapshot of VM #{${FIRECRACKER_HASH}}"
curl --unix-socket "${API_SOCKET}" -i \
    -X PUT 'http://localhost/snapshot/create' \
    -H  'Accept: application/json' \
    -H  'Content-Type: application/json' \
    -d '{
        "snapshot_type": "'$1'",
        "snapshot_path": "'$2'.snapshot.file",
        "mem_file_path": "'$2'.memory.file",
        "version": "1.1.0"
    }'

echo "Resuming VM #{${FIRECRACKER_HASH}}"
curl --unix-socket "${API_SOCKET}" -i \
    -X PATCH 'http://localhost/vm' \
    -H 'Accept: application/json' \
    -H 'Content-Type: application/json' \
    -d '{
        "state": "Resumed"
    }'
