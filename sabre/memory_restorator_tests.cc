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
static constexpr int _g_log_severity_ = LOG_INFO;
}

class MemoryRestoratorTest : public testing::Test {
protected:
  MemoryRestoratorTest() {
    qpl_path_t qpl_exec_path = qpl_path_hardware;
    if (std::getenv("SABRE_TEST_SOFTWARE_PATH"))
      qpl_exec_path = qpl_path_software;

    RLOG(1) << "Running tests with "
            << (qpl_exec_path == qpl_path_hardware ? "hardware" : "software")
            << " execution path.";

    acc::MemoryRestorator::MemoryRestoratotConfig cfg_scattered_dynamic = {
        .execution_path = qpl_exec_path,
        .partition_hanlding_path =
            acc::MemoryRestorator::kHandleAsScatteredPartitions,
        .scattered_partition_handling_path =
            acc::MemoryRestorator::kDoDynamicHuffmanForScatteredPartitions,
        .sigle_partition_handling_path =
            acc::MemoryRestorator::kHandleWithUffdioCopy,
        .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
        .max_hardware_jobs = 1,
        .passthrough = false};

    acc::MemoryRestorator::MemoryRestoratotConfig cfg_scattered_static = {
        .execution_path = qpl_exec_path,
        .partition_hanlding_path =
            acc::MemoryRestorator::kHandleAsScatteredPartitions,
        .scattered_partition_handling_path =
            acc::MemoryRestorator::kDoStaticHuffmanForScatteredPartitions,
        .sigle_partition_handling_path =
            acc::MemoryRestorator::kHandleWithUffdioCopy,
        .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
        .max_hardware_jobs = 1,
        .passthrough = false};

    acc::MemoryRestorator::MemoryRestoratotConfig cfg_single_uffdiocopy = {
        .execution_path = qpl_exec_path,
        .partition_hanlding_path =
            acc::MemoryRestorator::kHandleAsSinglePartition,
        .scattered_partition_handling_path =
            acc::MemoryRestorator::kDoDynamicHuffmanForScatteredPartitions,
        .sigle_partition_handling_path =
            acc::MemoryRestorator::kHandleWithUffdioCopy,
        .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
        .max_hardware_jobs = 1,
        .passthrough = false};

    memory_restorator_scattered_dynamic =
        std::unique_ptr<acc::MemoryRestorator>(
            new acc::MemoryRestorator(cfg_scattered_dynamic, "test", nullptr));
    memory_restorator_scattered_dynamic->Init();

    memory_restorator_scattered_static = std::unique_ptr<acc::MemoryRestorator>(
        new acc::MemoryRestorator(cfg_scattered_static, "test", nullptr));
    memory_restorator_scattered_static->Init();

    memory_restorator_single_uffdiocopy =
        std::unique_ptr<acc::MemoryRestorator>(
            new acc::MemoryRestorator(cfg_single_uffdiocopy, "test", nullptr));
    memory_restorator_single_uffdiocopy->Init();
  }

  ~MemoryRestoratorTest() {}

  // Main testing function.
  void
  makeAndRestoreSnapshot(acc::MemoryRestorator *memory_restorator,
                         const acc::MemoryRestorator::MemoryPartitions &memory,
                         size_t mem_size) {
    auto restored_memory_buffer = utils::m_mmap::nil;

    // Make a snapshot.
    EXPECT_TRUE(0 == memory_restorator->MakeSnapshot(
                         memory, reinterpret_cast<uint64_t>(
                                     std::get<0>(memory.front()))));

    // Drop snapshot caches.
    EXPECT_TRUE(0 == memory_restorator->DropCaches(true));

    // Restore from snapshot.
    EXPECT_TRUE(0 == memory_restorator->RestoreFromSnapshot(
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

  // Memory restorators under test.
  std::unique_ptr<acc::MemoryRestorator> memory_restorator_scattered_dynamic;
  std::unique_ptr<acc::MemoryRestorator> memory_restorator_scattered_static;
  std::unique_ptr<acc::MemoryRestorator> memory_restorator_single_uffdiocopy;
};

TEST_F(MemoryRestoratorTest, RandomPartitions) {
  std::mt19937 gen{123};

  // Create memory partitions.
  size_t mem_size = 256 * utils::kMB;
  size_t partition_n = 128;
  auto memory_buffer = utils::m_malloc::allocate(mem_size);
  auto true_entropy = init_rand_memory(memory_buffer.get(), mem_size, 100);

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
  static acc::MemoryRestorator::MemoryPartitions memory;
  for (auto const &[p_ptr, p_size] : memory_gen) {
    if (p_ptr != nullptr)
      memory.push_back(std::make_tuple(p_ptr, p_size));
  }

  makeAndRestoreSnapshot(this->memory_restorator_scattered_dynamic.get(),
                         memory, mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_scattered_static.get(), memory,
                         mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_single_uffdiocopy.get(),
                         memory, mem_size);
}

TEST_F(MemoryRestoratorTest, SparsePartitions) {

  size_t mem_size = 256 * utils::kMB;
  auto memory_buffer = utils::m_malloc::allocate(mem_size);
  auto true_entropy = init_rand_memory(memory_buffer.get(), mem_size, 100);

  size_t num_pages = std::ceil(mem_size / sys::kPageSize);

  static acc::MemoryRestorator::MemoryPartitions memory;
  for (int page_num = 0; page_num < num_pages; page_num++) {
    if (page_num % 10000 == 0) {
      auto p_ptr = memory_buffer.get() + page_num * sys::kPageSize;
      memory.push_back(std::make_tuple(p_ptr, sys::kPageSize));
    }
  }

  makeAndRestoreSnapshot(this->memory_restorator_scattered_dynamic.get(),
                         memory, mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_scattered_static.get(), memory,
                         mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_single_uffdiocopy.get(),
                         memory, mem_size);
}

TEST_F(MemoryRestoratorTest, EveryOtherPage) {

  size_t mem_size = 256 * utils::kMB;
  auto memory_buffer = utils::m_malloc::allocate(mem_size);
  auto true_entropy = init_rand_memory(memory_buffer.get(), mem_size, 100);

  size_t num_pages = std::ceil(mem_size / sys::kPageSize);

  static acc::MemoryRestorator::MemoryPartitions memory;
  for (int page_num = 0; page_num < num_pages; page_num++) {
    if (page_num % 2 == 0) {
      auto p_ptr = memory_buffer.get() + page_num * sys::kPageSize;
      memory.push_back(std::make_tuple(p_ptr, sys::kPageSize));
    }
  }

  makeAndRestoreSnapshot(this->memory_restorator_scattered_dynamic.get(),
                         memory, mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_scattered_static.get(), memory,
                         mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_single_uffdiocopy.get(),
                         memory, mem_size);
}

TEST_F(MemoryRestoratorTest, FirstPartitionWithNonZeroOffset) {

  size_t mem_size = 256 * utils::kMB;
  auto memory_buffer = utils::m_malloc::allocate(mem_size);
  auto true_entropy = init_rand_memory(memory_buffer.get(), mem_size, 100);

  size_t num_pages = std::ceil(mem_size / sys::kPageSize);

  static acc::MemoryRestorator::MemoryPartitions memory;
  for (int page_num = 0; page_num < num_pages; page_num++) {
    if (page_num % 2 == 1) {
      auto p_ptr = memory_buffer.get() + page_num * sys::kPageSize;
      memory.push_back(std::make_tuple(p_ptr, sys::kPageSize));
    }
  }

  makeAndRestoreSnapshot(this->memory_restorator_scattered_dynamic.get(),
                         memory, mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_scattered_static.get(), memory,
                         mem_size);
  makeAndRestoreSnapshot(this->memory_restorator_single_uffdiocopy.get(),
                         memory, mem_size);
}
