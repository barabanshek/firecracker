#!/bin/bash

sudo setfacl -m u:${USER}:rw /dev/kvm

curl --unix-socket /tmp/firecracker.socket -i \
    -X PUT 'http://localhost/snapshot/load' \
    -H  'Accept: application/json' \
    -H  'Content-Type: application/json' \
    -d '{
        "snapshot_type": "'$1'",
        "snapshot_path": "'$2'.snapshot.file",
        "mem_file_path": "'$2'.memory.file",
        "resume_vm": true
    }'
