# Sabre plugin for Firecracker

Implementation of Sabre plugin for Firecracker described in the paper ["Sabre: Improving Memory Prefetching in Serverless MicroVMs with Near-Memory Hardware-Accelerated Compression"]().

The plugin is exposed to Firecracker via Rust FFI through a set of small changes in the original Firecracker's codebase outside of this folder. The full diff: `git diff 5326773`.

## Dependencies
* docker
* glibc-2.35 or higher


## Build Firecracker with Sabre

Sabre is integrated in standard Firecracker's Docker development container. Only *GNU glibc* builds are currently supported:

```
# Run in root folder of the firecracker repository

docker pull barabanshik/firecracker_sabre:latest
git clone https://github.com/barabanshek/firecracker.git
git fetch origin sabre; git checkout sabre
tools/devtool build --release --libc gnu
```

To build your own development container, execute:
```
tools/devtool build_devctr
```


## Running Sabre demo

This is more for fun and debug/early testing (before we got unit tests).

```
sudo ./build/sabre/memory_restorator_demo
```

## Running unit tests
```
# export SABRE_TEST_SOFTWARE_PATH=1 to use software compression for testing
sudo -E ./build/sabre/memory_restorator_test
```

For regression, even if all tests pass, one still must run the microbenchmark to test the changes do not make things slower.

## Running integration tests

Sabe is integrated with Firecracker via Rust FFI (`git diff 5326773`). For now, we don't have good integration tests, but one can run a sequence of simple REST API calls to boot/snapshot/restore in different configurations.
```
pushd examples

# Get sample guest kernel and rootfs
ARCH="$(uname -m)"
wget https://s3.amazonaws.com/spec.ccfc.min/firecracker-ci/v1.8/${ARCH}/vmlinux-5.10.210
wget https://s3.amazonaws.com/spec.ccfc.min/firecracker-ci/v1.8/${ARCH}/ubuntu-22.04.ext4

# Run test scripts
# Test Full snapshots
sudo ./test.sh <path to kernel/vmlinux> <path to rootfs>
# Test Diff snapshots
sudo ./test_diff.sh <path to kernel/vmlinux> <path to rootfs>

popd
```

## Running microbenchmark

The microbenchmark is designed to reproduce *Figure 9* from the paper. It runs Sabre over snapshots of different sparsities (over the specified dataset) and in different modes. For the best results, use real uVM snapshots as the datasets.

```
# Run in root folder of the firecracker repository

# Configure the machine and setup the IAA hardware;
#   - use <CPU frequency> of 2700000 to reproduce results from the paper and the figure bellow
pushd sabre/scripts/; sudo ./setup_node.sh <CPU frequency>; popd

# Export the dataset location and name
export SABRE_DATASET_PATH=<path to the dataset dir>
export SABRE_DATASET_NAME=<name of the dataset>

# Run benchmark (at least <N> = 3 times for best results)
sudo -E ./build/sabre/memory_restoration_micro --benchmark_repetitions=<N> --benchmark_min_time=1x --benchmark_format=csv --logtostderr | tee results.csv

# Plot results
python3 sabre/scripts/plot_microbenchmark.py results.csv
```

This reproduces the follwoing characterization of Sabre memory restoration:

![handling](images/handling_plots.png)

## Running with firecracker-containerd

[Firecracker-containerd](https://github.com/firecracker-microvm/firecracker-containerd) allows to run regular docker containers in firecracker. To try it, run the following:

```
pushd containerd/

# Prepare machine to run containerd
./install_contrainerd.sh

# Setup env to run containerd (if fails - run one more time)
./configure_node_for_containerd.sh

# Try it out
sudo firecracker-containerd --config /etc/firecracker-containerd/config.toml

# In another window
sudo firecracker-ctr --address /run/firecracker-containerd/containerd.sock image pull --snapshotter devmapper docker.io/library/hello-world:latest
sudo firecracker-ctr --address /run/firecracker-containerd/containerd.sock run --snapshotter devmapper --runtime aws.firecracker --rm --tty --net-host docker.io/library/hello-world:latest test
```

Please, rerfer to [original documentation](https://github.com/firecracker-microvm/firecracker-containerd/blob/main/docs/getting-started.md) to hack around firecracker-containerd.

A special credit to [vHive project](https://github.com/vhive-serverless/vHive) for the inspiration!
