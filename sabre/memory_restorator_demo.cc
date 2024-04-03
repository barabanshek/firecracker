#include <algorithm>
#include <cassert>
#include <random>

#include <gflags/gflags.h>

#include "memory_restorator.h"
#include "simple_logging.h"
#include "test_utils.h"
#include "utils.h"

// Init logging infra.
namespace logging {
static constexpr int _g_log_severity_ = LOG_INFO;
}

//
DEFINE_bool(use_mempool, false,
            "Use mempool for decompression buffers or not.");
DEFINE_bool(memory_owner, true,
            "Who owns memory: 0 - memory_restorator, 1 - application.");
DEFINE_bool(drop_caches, true, "Drop cache flag.");
DEFINE_uint64(mem_size, 256 * utils::kMB,
              "Size of memory to compress (in Bytes).");
DEFINE_uint64(mem_entropy, 100, "Entropy of generated memory.");
DEFINE_uint64(partition_n, 16, "Number of partitions in source.");
DEFINE_uint64(partition_seed, 123, "Seed in rng for partitioning.");
DEFINE_string(snapshot_filename, "mysnapshot", "Where to write snapshot data.");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Init rnd.
  std::mt19937 gen{FLAGS_partition_seed};

  // Create memory partitions.
  size_t mem_size = FLAGS_mem_size;
  size_t partition_n = FLAGS_partition_n;
  auto memory_buffer = utils::malloc_allocate(mem_size);
  auto true_entropy =
      init_rand_memory(memory_buffer.get(), mem_size, FLAGS_mem_entropy);
  RLOG(LOG_INFO) << "Memory generated with true entropy: " << true_entropy;

  acc::MemoryRestorator::MemoryPartitions memory_gen;
  std::uniform_int_distribution<size_t> generator_uniform(1, mem_size - 1);
  std::vector<size_t> delimeters;
  for (size_t i = 0; i < partition_n - 1; ++i)
    delimeters.push_back(generator_uniform(gen) & ~(sys::kPageSize - 1));
  std::sort(delimeters.begin(), delimeters.end());

  size_t p_offset = 0;
  size_t p_size = 0;
  std::uniform_int_distribution<size_t> mask_generator_uniform(0, 5);
  for (auto const &d : delimeters) {
    p_size = d - p_offset;
    if (mask_generator_uniform(gen) != 0) {
      memory_gen.push_back(
          std::make_tuple(memory_buffer.get() + p_offset, p_size));
    } else {
      memory_gen.push_back(std::make_tuple(nullptr, p_size));
    }
    p_offset += p_size;
  }
  memory_gen.push_back(
      std::make_tuple(memory_buffer.get() + p_offset, mem_size - p_offset));

  // Check memory partitions.
  size_t check_total_size = 0;
  uint8_t *p_begin = memory_buffer.get();
  for (auto const &[p_ptr, p_size] : memory_gen) {
    assert(p_size >= sys::kPageSize);
    if (p_ptr != nullptr)
      assert(p_ptr == p_begin);
    check_total_size += p_size;
    p_begin += p_size;
  }
  assert(check_total_size == mem_size);

  // Remove nullptr partitions (we needed them just for the check).
  acc::MemoryRestorator::MemoryPartitions memory;
  for (auto const &[p_ptr, p_size] : memory_gen) {
    if (p_ptr != nullptr)
      memory.push_back(std::make_tuple(p_ptr, p_size));
  }

  // Dump memory partitions.
  RLOG(LOG_VERBAL) << "partitions created: ";
  for (auto const &[p_ptr, p_size] : memory) {
    RLOG(LOG_VERBAL) << "    " << (void *)(p_ptr) << ": " << p_size;
  }

  // Allocate mempool.
  MemoryPool mem_pool;
  if (FLAGS_use_mempool) {
    static constexpr size_t kMemPoolSize = 1024 * 1024 * 1024;
    mem_pool.Init(kMemPoolSize);
  }

  // Do things.
  // Configure memory restorator.
  acc::MemoryRestorator::MemoryRestoratotConfig cfg = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsScatteredPartitions,
      .scattered_partition_handling_path =
          acc::MemoryRestorator::kDoDynamicHuffmanForScatteredPartitions,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .restored_memory_owner = FLAGS_memory_owner == true
                                   ? acc::MemoryRestorator::kUserApplication
                                   : acc::MemoryRestorator::kMemoryRestorator,
      .max_hardware_jobs = 1,
      .passthrough = false};

  acc::MemoryRestorator memory_restorator(cfg, FLAGS_snapshot_filename.c_str(),
                                          FLAGS_use_mempool ? &mem_pool
                                                            : nullptr);
  if (memory_restorator.Init()) {
    RLOG(LOG_ERROR) << "Failed to init QPL.";
  }
  if (memory_restorator.MakeSnapshot(
          memory, reinterpret_cast<uint64_t>(std::get<0>(memory.front())))) {
    RLOG(LOG_ERROR) << "Failed to make snapshot.";
  }

  // Drop caches.
  if (FLAGS_drop_caches) {
    if (memory_restorator.DropCaches()) {
      RLOG(LOG_ERROR) << "Failed to drop caches.";
    }
  }

  auto restored_memory_buffer =
      std::unique_ptr<uint8_t, utils::MMapDeleter>(nullptr);
  if (FLAGS_memory_owner == 1)
    restored_memory_buffer = utils::mmap_allocate(mem_size);

  if (memory_restorator.RestoreFromSnapshot(restored_memory_buffer, mem_size,
                                            nullptr)) {
    RLOG(LOG_ERROR) << "Failed to restore memory.";
    return -1;
  }

  // Compare.
  size_t i = 0;
  auto p_ptr_begin = std::get<0>(memory.front());
  for (auto const &[p_ptr, p_size] : memory) {
    if (memcmp(p_ptr,
               restored_memory_buffer.get() +
                   (reinterpret_cast<uint64_t>(p_ptr) -
                    reinterpret_cast<uint64_t>(p_ptr_begin)),
               p_size)) {
      RLOG(LOG_ERROR) << "Data missmatch in partition: " << i;
    }
    ++i;
  }

  RLOG(LOG_INFO) << "All good!";
  return 0;
}
