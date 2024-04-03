# Sabre plugin for Firecracker

Implementation of Sabre plugin for Firecracker described in the paper ["Sabre: Improving Memory Prefetching in Serverless MicroVMs with Near-Memory Hardware-Accelerated Compression"]().

The full diff:
```
git diff 5326773
```

## Build Firecracker with Sabre

Sabre is integrated in standard Firecracker's Docker development container. To build:

```
docker pull barabanshik/firecracker_sabre:latest
git clone https://github.com/barabanshek/firecracker.git
git fetch origin sabre; git checkout sabre
tools/devtool build --release --libc gnu
```
