#!/bin/bash

sudo setfacl -m u:${USER}:rw /dev/kvm

echo "Pausing..."
curl --unix-socket /tmp/firecracker.socket -i \
    -X PATCH 'http://localhost/vm' \
    -H 'Accept: application/json' \
    -H 'Content-Type: application/json' \
    -d '{
        "state": "Paused"
    }'

echo "Making snapshot...: "
curl --unix-socket /tmp/firecracker.socket -i \
    -X PUT 'http://localhost/snapshot/create' \
    -H  'Accept: application/json' \
    -H  'Content-Type: application/json' \
    -d '{
        "snapshot_type": "'$1'",
        "snapshot_path": "'$2'.snapshot.file",
        "mem_file_path": "'$2'.memory.file",
        "version": "1.5.0"
    }'

echo "Resuming..."
curl --unix-socket /tmp/firecracker.socket -i \
    -X PATCH 'http://localhost/vm' \
    -H 'Accept: application/json' \
    -H 'Content-Type: application/json' \
    -d '{
        "state": "Resumed"
    }'
