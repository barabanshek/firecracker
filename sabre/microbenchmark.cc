#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>

#include <benchmark/benchmark.h>

#include "memory_restorator.h"
#include "simple_logging.h"
#include "utils.h"

// Init logging infra.
namespace logging {
static constexpr int _g_log_severity_ = LOG_ERROR;
}

typedef std::map<std::string, std::tuple<size_t, uint8_t *>> CompressionDataset;

static std::map<size_t,
                std::tuple<acc::MemoryRestorator::MemoryPartitions, size_t>>
    sub_datasets;

static CompressionDataset load_corpus_dataset(const char *dataset_path) {
  assert(std::filesystem::exists(dataset_path) &&
         std::filesystem::is_directory(dataset_path));
  RLOG(LOG_INFO) << "Loading dataset from " << dataset_path;
  CompressionDataset dataset;
  for (const auto &entry : std::filesystem::directory_iterator(dataset_path)) {
    std::string filename_ = entry.path().filename();
    std::string filename = std::string(dataset_path) + "/" + filename_;
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1)
      RLOG(LOG_ERROR) << "failed to open benchmark file " << filename << ", "
                      << strerror(errno);
    size_t fd_size = static_cast<size_t>(lseek(fd, 0L, SEEK_END));
    lseek(fd, 0L, SEEK_SET);
    RLOG(LOG_INFO) << "Found file: " << filename << " of size: " << fd_size
                   << " B";
    uint8_t *mem = reinterpret_cast<uint8_t *>(malloc(fd_size));
    if (read(fd, mem, fd_size) != fd_size)
      RLOG(LOG_ERROR) << "Failed to read benchmark file " << filename;
    dataset[filename_] = std::make_tuple(fd_size, mem);
  }
  RLOG(LOG_INFO) << "Dataset with " << dataset.size() << " files is loaded";
  return dataset;
}

void make_datasets(const char *dataset_prefix, const char *dataset_name,
                   const std::vector<size_t> &sparsities) {
  // Read dataset.
  auto d_set = load_corpus_dataset(dataset_prefix);
  auto const &[mem_size, source_buff] = d_set[dataset_name];

  // Cut dataset to produce sub-datasets.
  size_t n_of_pages = std::ceil(mem_size / sys::kPageSize);
  RLOG(LOG_INFO) << "Using dataset: " << dataset_name << ", @" << std::hex
                 << (void *)(source_buff) << ": " << std::dec << n_of_pages;
  for (auto const &sparsity : sparsities) {
    RLOG(LOG_INFO) << "Making dataset of sparsity #" << sparsity;

    acc::MemoryRestorator::MemoryPartitions dataset_partitions;
    size_t dataset_size = mem_size * 2;
    auto *dataset_mem = reinterpret_cast<uint8_t *>(malloc(dataset_size));
    assert(dataset_mem != nullptr);

    size_t page_i = 0;
    size_t page_j = 0;
    while (page_j < n_of_pages) {
      // Pages.
      auto dataset_mem_ptr = dataset_mem + page_i * sys::kPageSize;
      auto partition_size = sparsity * sys::kPageSize;
      if (page_j > n_of_pages - sparsity)
        partition_size = (n_of_pages - page_j) * sys::kPageSize;
      std::memcpy(dataset_mem_ptr, source_buff + page_j * sys::kPageSize,
                  partition_size);
      // Zeros.
      std::memset(dataset_mem_ptr + partition_size, 0, sys::kPageSize);
      // Increment.
      page_i += (sparsity + 1);
      page_j += sparsity;
      // Append to partitions.
      dataset_partitions.push_back(
          std::make_tuple(dataset_mem_ptr, partition_size));
    }
    // Append to dataset.
    sub_datasets[sparsity] = std::make_tuple(dataset_partitions, dataset_size);
  }
}

auto BM_BenchmarkMemoryRestorator = [](benchmark::State &state,
                                       auto Inputs...) {
  va_list args;
  va_start(args, Inputs);
  auto execution_path = Inputs;
  auto sparsity = va_arg(args, size_t);
  auto handling = va_arg(args, int);
  auto passthrough = va_arg(args, int);
  va_end(args);

  // Get dataset.
  auto const &[partitions, total_size] = sub_datasets[sparsity];

  // Do things.
  acc::MemoryRestorator::MemoryRestoratotConfig cfg = {
      .execution_path = execution_path,
      .partition_hanlding_path =
          static_cast<acc::MemoryRestorator::PartitionHandlingPath>(handling),
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .restored_memory_owner = acc::MemoryRestorator::kUserApplication,
      .max_hardware_jobs = 1,
      .passthrough = static_cast<bool>(passthrough)};

  acc::MemoryRestorator memory_restorator(cfg, "dummysnapshot");
  if (memory_restorator.Init()) {
    state.SkipWithMessage("Failed to initialize memory restorator.");
  }

  if (memory_restorator.MakeSnapshot(
          partitions,
          reinterpret_cast<uint64_t>(std::get<0>(partitions.front())))) {
    state.SkipWithMessage("Failed to make snapshot.");
  }

  if (memory_restorator.DropCaches()) {
    state.SkipWithMessage("Failed to drop caches.");
  }

  auto restored_memory_buffer = utils::m_mmap::allocate(total_size);
  assert(restored_memory_buffer.get() != nullptr);
  for (auto _ : state) {
    if (memory_restorator.RestoreFromSnapshot(restored_memory_buffer,
                                              total_size, nullptr)) {
      state.SkipWithMessage("Failed to restore from snapshot.");
    }
  }

  // Append stat.
  auto stat = memory_restorator.GetMetrics();
  state.counters["mmap_dst_mem"] = stat.mmap_dst_mem;
  state.counters["get_partition_info"] = stat.get_partition_info;
  state.counters["mmap_snapshot"] = stat.mmap_snapshot;
  state.counters["mmap_decompression_buff"] = stat.mmap_decompression_buff;
  state.counters["decompress"] = stat.decompress;
  state.counters["install_pages"] = stat.install_pages;
  state.counters["mem_restore_total"] = stat.mem_restore_total;

  // Compare.
  size_t i = 0;
  auto p_ptr_begin = std::get<0>(partitions.front());
  for (auto const &[p_ptr, p_size] : partitions) {
    if (memcmp(p_ptr,
               restored_memory_buffer.get() +
                   (reinterpret_cast<uint64_t>(p_ptr) -
                    reinterpret_cast<uint64_t>(p_ptr_begin)),
               p_size)) {
      state.SkipWithMessage("Data missmatch.");
    }
    ++i;
  }
};

// Usage:
//   - export SABRE_DATASET_PATH=...
//   - export SABRE_DATASET_NAME=...
//   - sudo -E ./build/sabre/memory_restoration_micro --benchmark_repetitions=10 --benchmark_min_time=1x
int main(int argc, char **argv) {
  // Setup.
  std::vector<size_t> sparsities = {1,  2,   4,    10,   20,
                                    50, 100, 1000, 5000, 20000};

  const char *dataset_path = std::getenv("SABRE_DATASET_PATH");
  const char *dataset_name = std::getenv("SABRE_DATASET_NAME");
  if (dataset_path == nullptr || dataset_name == nullptr) {
    RLOG(LOG_ERROR) << "Empty dataset path.";
    return -1;
  }

  make_datasets(dataset_path, dataset_name, sparsities);

  // Register benchmarks.
  for (auto const &sparsity : sparsities) {
    for (auto const &handling :
         {acc::MemoryRestorator::kHandleAsSinglePartition,
          acc::MemoryRestorator::kHandleAsScatteredPartitions}) {
      for (int passthroug : {0, 1}) {
        if (passthroug &&
            handling == acc::MemoryRestorator::kHandleAsScatteredPartitions)
          continue;

        for (auto const &path : {qpl_path_hardware, qpl_path_software}) {
          benchmark::RegisterBenchmark(
              std::string("BM_BenchmarkMemoryRestorator") + "_sparsity_" +
                  std::to_string(sparsity) + "_handling_" +
                  std::to_string(handling) + "_passthrough_" +
                  std::to_string(passthroug) + "_path_" +
                  (path == qpl_path_hardware ? "qpl_path_hardware"
                                             : "qpl_path_software"),
              BM_BenchmarkMemoryRestorator, path, sparsity, handling,
              passthroug);
        }
      }
    }
  }

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
