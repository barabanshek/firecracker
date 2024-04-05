#include <gtest/gtest.h>

#include <algorithm>
#include <cassert>
#include <random>

#include "memory_restorator.h"
#include "simple_logging.h"
#include "test_utils.h"
#include "utils.h"

// Init logging infra.
namespace logging {
static constexpr int _g_log_severity_ = LOG_ERROR;
}

class MemoryRestoratorTest : public testing::Test {
protected:
  MemoryRestoratorTest() {
    qpl_path_t qpl_exec_path = qpl_path_hardware;
    if (std::getenv("SABRE_TEST_SOFTWARE_PATH"))
      qpl_exec_path = qpl_path_software;

    RLOG(LOG_INFO) << "Running tests with "
                   << (qpl_exec_path == qpl_path_hardware ? "hardware"
                                                          : "software")
                   << " execution path.";

    cfg_scattered_dynamic.execution_path = qpl_exec_path;
    cfg_scattered_static.execution_path = qpl_exec_path;
    cfg_single_uffdiocopy.execution_path = qpl_exec_path;
    cfg_scattered_static_app_owner.execution_path = qpl_exec_path;
    cfg_single_uffdiocopy_app_owner.execution_path = qpl_exec_path;
    cfg_single_passthrough.execution_path = qpl_exec_path;
    cfg_scattered_static_multijob.execution_path = qpl_exec_path;
  }

  ~MemoryRestoratorTest() {}

  // Helpers.
  /// Randomly split @param memory_buffer of size @param mem_size into @param
  /// partition_n; populate with random data with entropy @param
  /// compressability, and store the result in the partition map @param
  /// partitions.
  void initRandomPartitions(
      const utils::m_malloc::Memory &memory_buffer, size_t mem_size,
      size_t partition_n, uint16_t compressability,
      acc::MemoryRestorator::MemoryPartitions &partitions) const {
    std::mt19937 gen{123};

    // Create memory partitions.
    auto true_entropy =
        init_rand_memory(memory_buffer.get(), mem_size, compressability);
    (void)true_entropy;

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
    for (auto const &[p_ptr, p_size] : memory_gen) {
      if (p_ptr != nullptr)
        partitions.push_back(std::make_tuple(p_ptr, p_size));
    }
  }

  void initSparsePartitions(
      const utils::m_malloc::Memory &memory_buffer, size_t mem_size,
      uint16_t sparsity, int offset,
      acc::MemoryRestorator::MemoryPartitions &partitions) const {
    size_t num_pages = std::ceil(mem_size / sys::kPageSize);
    for (size_t page_num = 0; page_num < num_pages; page_num++) {
      if (page_num % sparsity == offset) {
        auto p_ptr = memory_buffer.get() + page_num * sys::kPageSize;
        partitions.push_back(std::make_tuple(p_ptr, sys::kPageSize));
      }
    }
  }

  // Main testing function.
  void makeAndRestoreSnapshot(
      const acc::MemoryRestorator::MemoryRestoratotConfig &cfg,
      const acc::MemoryRestorator::MemoryPartitions &memory, size_t mem_size,
      bool app_owner = false) const {
    auto restored_memory_buffer = utils::m_mmap::nil;
    if (app_owner)
      restored_memory_buffer = utils::m_mmap::allocate(mem_size);

    // Init memory restorator.
    acc::MemoryRestorator memory_restorator(cfg, "test", nullptr);
    EXPECT_TRUE(0 == memory_restorator.Init());

    // Make a snapshot.
    EXPECT_TRUE(0 == memory_restorator.MakeSnapshot(
                         memory, reinterpret_cast<uint64_t>(
                                     std::get<0>(memory.front()))));

    // Drop snapshot caches.
    EXPECT_TRUE(0 == memory_restorator.DropCaches(true));

    // Restore from snapshot.
    EXPECT_TRUE(0 == memory_restorator.RestoreFromSnapshot(
                         restored_memory_buffer, mem_size, nullptr));

    // Compare results.
    auto p_ptr_begin = std::get<0>(memory.front());
    for (auto const &[p_ptr, p_size] : memory) {
      EXPECT_TRUE(0 ==
                  std::memcmp(p_ptr,
                              restored_memory_buffer.get() +
                                  (reinterpret_cast<uint64_t>(p_ptr) -
                                   reinterpret_cast<uint64_t>(p_ptr_begin)),
                              p_size));
    }
  }

  // Configs.
  acc::MemoryRestorator::MemoryRestoratotConfig cfg_scattered_dynamic = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsScatteredPartitions,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .scattered_partition_handling_path =
          acc::MemoryRestorator::kDoDynamicHuffmanForScatteredPartitions,
      .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
      .max_hardware_jobs = 1,
      .passthrough = false};

  acc::MemoryRestorator::MemoryRestoratotConfig cfg_scattered_static = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsScatteredPartitions,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .scattered_partition_handling_path =
          acc::MemoryRestorator::kDoStaticHuffmanForScatteredPartitions,
      .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
      .max_hardware_jobs = 1,
      .passthrough = false};

  acc::MemoryRestorator::MemoryRestoratotConfig cfg_single_uffdiocopy = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsSinglePartition,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .scattered_partition_handling_path =
          acc::MemoryRestorator::kDoDynamicHuffmanForScatteredPartitions,
      .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
      .max_hardware_jobs = 1,
      .passthrough = false};

  acc::MemoryRestorator::MemoryRestoratotConfig cfg_scattered_static_app_owner =
      {.execution_path = qpl_path_hardware,
       .partition_hanlding_path =
           acc::MemoryRestorator::kHandleAsScatteredPartitions,
       .sigle_partition_handling_path =
           acc::MemoryRestorator::kHandleWithUffdioCopy,
       .scattered_partition_handling_path =
           acc::MemoryRestorator::kDoStaticHuffmanForScatteredPartitions,
       .restored_memory_owner = acc::MemoryRestorator::kUserApplication,
       .max_hardware_jobs = 1,
       .passthrough = false};

  acc::MemoryRestorator::MemoryRestoratotConfig
      cfg_single_uffdiocopy_app_owner = {
          .execution_path = qpl_path_hardware,
          .partition_hanlding_path =
              acc::MemoryRestorator::kHandleAsSinglePartition,
          .sigle_partition_handling_path =
              acc::MemoryRestorator::kHandleWithUffdioCopy,
          .scattered_partition_handling_path =
              acc::MemoryRestorator::kDoStaticHuffmanForScatteredPartitions,
          .restored_memory_owner = acc::MemoryRestorator::kUserApplication,
          .max_hardware_jobs = 1,
          .passthrough = false};

  acc::MemoryRestorator::MemoryRestoratotConfig cfg_single_passthrough = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsSinglePartition,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .scattered_partition_handling_path =
          acc::MemoryRestorator::kDoDynamicHuffmanForScatteredPartitions,
      .restored_memory_owner = acc::MemoryRestorator::kUserApplication,
      .max_hardware_jobs = 1,
      .passthrough = true};

  acc::MemoryRestorator::MemoryRestoratotConfig cfg_scattered_static_multijob =
      {.execution_path = qpl_path_hardware,
       .partition_hanlding_path =
           acc::MemoryRestorator::kHandleAsScatteredPartitions,
       .sigle_partition_handling_path =
           acc::MemoryRestorator::kHandleWithUffdioCopy,
       .scattered_partition_handling_path =
           acc::MemoryRestorator::kDoStaticHuffmanForScatteredPartitions,
       .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
       .max_hardware_jobs = 4,
       .passthrough = false};
};

// Common defines
#define MEMBUF_256                                                             \
  size_t mem_size = 256 * utils::kMB;                                          \
  auto memory_buffer = utils::m_malloc::allocate(mem_size);                    \
  acc::MemoryRestorator::MemoryPartitions memory_partitions;

#define MEMBUF_1024                                                            \
  size_t mem_size = 1024 * utils::kMB;                                         \
  auto memory_buffer = utils::m_malloc::allocate(mem_size);                    \
  acc::MemoryRestorator::MemoryPartitions memory_partitions;

// Tests.
TEST_F(MemoryRestoratorTest, RandomPartitions_ScatteredDynamic) {
  MEMBUF_256
  initRandomPartitions(memory_buffer, mem_size, 128, 100, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_dynamic, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, RandomPartitions_ScatteredStatic) {
  MEMBUF_256
  initRandomPartitions(memory_buffer, mem_size, 128, 100, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, RandomPartitions_ScatteredStaticCompressible) {
  MEMBUF_256
  initRandomPartitions(memory_buffer, mem_size, 128, 5, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest,
       RandomPartitions_ScatteredStaticCompressibleHugeMemory) {
  MEMBUF_1024
  initRandomPartitions(memory_buffer, mem_size, 128, 5, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, RandomPartitions_SingleUffdiocopy) {
  MEMBUF_256
  initRandomPartitions(memory_buffer, mem_size, 128, 100, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_uffdiocopy, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest,
       RandomPartitions_SingleUffdiocopyCompressibleHugeMemory) {
  MEMBUF_1024
  initRandomPartitions(memory_buffer, mem_size, 128, 5, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_uffdiocopy, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, SparsePartitions_ScatteredDynamic) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 10000, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_dynamic, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, SparsePartitions_ScatteredStatic) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 10000, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, SparsePartitions_SingleUffdiocopy) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 10000, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_uffdiocopy, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage_ScatteredDynamic) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_dynamic, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage_ScatteredStatic) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage_SingleUffdiocopy) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_uffdiocopy, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage_ScatteredDynamicNonZeroOffset) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 1, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_dynamic, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage_ScatteredStaticNonZeroOffset) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 1, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage_SingleUffdiocopyNonZeroOffset) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 1, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_uffdiocopy, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest, ScatteredStaticAppOwner) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static_app_owner,
                         memory_partitions, mem_size, true);
}

TEST_F(MemoryRestoratorTest, SingleUffdiocopyAppOwner) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_uffdiocopy_app_owner,
                         memory_partitions, mem_size, true);
}

TEST_F(MemoryRestoratorTest, PassthroughStartingFromZeroPageOffset) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_passthrough, memory_partitions,
                         mem_size, true);
}

TEST_F(MemoryRestoratorTest, PassthroughStartingFromNonZeroPageOffset) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 1, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_single_passthrough, memory_partitions,
                         mem_size, true);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage_ScatteredStaticMultiJob) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 0, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static_multijob, memory_partitions,
                         mem_size);
}

TEST_F(MemoryRestoratorTest,
       EveryOtherPage_ScatteredStaticMultiJobNonZeroOffset) {
  MEMBUF_256
  initSparsePartitions(memory_buffer, mem_size, 2, 1, memory_partitions);
  makeAndRestoreSnapshot(this->cfg_scattered_static_multijob, memory_partitions,
                         mem_size);
}
