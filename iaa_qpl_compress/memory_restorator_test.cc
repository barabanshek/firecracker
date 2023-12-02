#include <gtest/gtest.h>

#include <algorithm>
#include <cassert>
#include <random>

#include <gflags/gflags.h>

#include "memory_restorator.h"
#include "test_utils.h"
#include "utils.h"

#include <iostream>



class MemoryRestoratorTest : public testing::Test {
    protected:

    static void SetUpTestSuite() {

        acc::MemoryRestorator::MemoryRestoratotConfig cfg_scattered_dynamic = {
            .execution_path = qpl_path_software,
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
            .execution_path = qpl_path_software,
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
            .execution_path = qpl_path_software,
            .partition_hanlding_path =
                acc::MemoryRestorator::kHandleAsSinglePartition,
            .scattered_partition_handling_path =
                acc::MemoryRestorator::kDoDynamicHuffmanForScatteredPartitions,
            .sigle_partition_handling_path =
                acc::MemoryRestorator::kHandleWithUffdioCopy,
            .restored_memory_owner = acc::MemoryRestorator::kMemoryRestorator,
            .max_hardware_jobs = 1,
            .passthrough = false};


        if (memory_restorator_scattered_dynamic == nullptr) {
            memory_restorator_scattered_dynamic = new  acc::MemoryRestorator(cfg_scattered_dynamic, "test", nullptr);
            memory_restorator_scattered_dynamic->Init();            
        }
        if (memory_restorator_scattered_static == nullptr) {
            memory_restorator_scattered_static = new  acc::MemoryRestorator(cfg_scattered_static, "test", nullptr);
            memory_restorator_scattered_static->Init();            
        }
        if (memory_restorator_single_uffdiocopy == nullptr) {
            memory_restorator_single_uffdiocopy = new  acc::MemoryRestorator(cfg_single_uffdiocopy, "test", nullptr);
            memory_restorator_single_uffdiocopy->Init();            
        }
    } 

    static void TearDownTestSuite() {
        delete memory_restorator_scattered_dynamic;
        memory_restorator_scattered_dynamic = nullptr;  

        delete memory_restorator_scattered_static;
        memory_restorator_scattered_static = nullptr; 

        delete memory_restorator_single_uffdiocopy;
        memory_restorator_single_uffdiocopy = nullptr; 
    }

    void makeAndRestoreSnapshot(acc::MemoryRestorator* memory_restorator, 
                                acc::MemoryRestorator::MemoryPartitions memory, 
                                std::unique_ptr<uint8_t, utils::MMapDeleter> &restored_memory_buffer,
                                size_t mem_size) {
        EXPECT_TRUE( 0 == memory_restorator_scattered_dynamic->MakeSnapshot(
                memory, reinterpret_cast<uint64_t>(std::get<0>(memory.front()))));
        
        memory_restorator_scattered_dynamic->DropCaches();

        EXPECT_TRUE(0 == memory_restorator_scattered_dynamic->RestoreFromSnapshot(
                                                    restored_memory_buffer, mem_size,
                                                    nullptr));

        auto p_ptr_begin = std::get<0>(memory.front());
        for (auto const &[p_ptr, p_size] : memory) {
            EXPECT_TRUE( 0 == std::memcmp( p_ptr, restored_memory_buffer.get() +
                        (reinterpret_cast<uint64_t>(p_ptr) -
                            reinterpret_cast<uint64_t>(p_ptr_begin)), p_size) );
        }

        restored_memory_buffer.reset();
    }

    static acc::MemoryRestorator* memory_restorator_scattered_dynamic;
    static acc::MemoryRestorator* memory_restorator_scattered_static;
    static acc::MemoryRestorator* memory_restorator_single_uffdiocopy;
};


acc::MemoryRestorator* MemoryRestoratorTest::memory_restorator_scattered_dynamic = nullptr; 
acc::MemoryRestorator* MemoryRestoratorTest::memory_restorator_scattered_static = nullptr; 
acc::MemoryRestorator* MemoryRestoratorTest::memory_restorator_single_uffdiocopy = nullptr; 


// Demonstrate some basic assertions.
TEST_F( MemoryRestoratorTest, RandomPartitions) {
    std::mt19937 gen{123};
    // Create memory partitions.
    size_t mem_size = 256 * utils::kMB;
    size_t partition_n = 128;
    auto memory_buffer = utils::malloc_allocate(mem_size);
    auto true_entropy =
        init_rand_memory(memory_buffer.get(), mem_size, 100);

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

    auto restored_memory_buffer =
        std::unique_ptr<uint8_t, utils::MMapDeleter>(nullptr);

    makeAndRestoreSnapshot(memory_restorator_scattered_dynamic, memory,
                           restored_memory_buffer, mem_size);
    makeAndRestoreSnapshot(memory_restorator_scattered_dynamic, memory,
                           restored_memory_buffer, mem_size);
    makeAndRestoreSnapshot(memory_restorator_scattered_dynamic, memory,
                           restored_memory_buffer, mem_size);


}

