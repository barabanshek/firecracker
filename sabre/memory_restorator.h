#ifndef _MEMORY_RESTORATOR_H_
#define _MEMORY_RESTORATOR_H_

#include <cassert>
#include <vector>

#include "qpl/qpl.h"

#include "mem_pool.h"
#include "utils.h"

namespace acc {

class MemoryRestorator {
public:
  //
  // Configuration space.
  //
  typedef enum {
    kHandleAsSinglePartition,
    kHandleAsScatteredPartitions
  } PartitionHandlingPath;

  typedef enum {
    kHandleWithUffdioCopy,
    kHandleWithUffdioContinue // TODO(Nikita): this is currently broken as
                              // sparsed partitions can not be installed with
                              // UFFDIO_CONTINUE; This might work ~2x faster
                              // than UFFDIO_COPY, so we need to find a way of
                              // implementing it.
  } SinglePartitionHandlingPath;

  typedef enum {
    kDoDynamicHuffmanForScatteredPartitions,
    kDoStaticHuffmanForScatteredPartitions
  } ScatteredPartitionHandlingPath;

  // Who own the memory after restoration: this memory restorator or the user
  // application.
  typedef enum { kMemoryRestorator, kUserApplication } RestoredMemoryOwner;

  struct MemoryRestoratotConfig {
    qpl_path_t execution_path; // Do in qpl_path_hardware or qpl_path_software.
    PartitionHandlingPath partition_hanlding_path;
    SinglePartitionHandlingPath sigle_partition_handling_path;
    ScatteredPartitionHandlingPath scattered_partition_handling_path;
    RestoredMemoryOwner restored_memory_owner;
    uint8_t max_hardware_jobs;
    bool passthrough;
  };

  // Performance metrics to measure time in the page restoration critical path.
  struct Metrics {
    uint64_t mmap_dst_mem;
    uint64_t get_partition_info;
    uint64_t mmap_snapshot;
    uint64_t mmap_decompression_buff;
    uint64_t decompress;
    uint64_t install_pages;
    uint64_t mem_restore_total;
  };

  // This configuration shows the best performance of memory restoration on most
  // synthetic cases.
  static constexpr MemoryRestoratotConfig default_cfg = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsScatteredPartitions,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .restored_memory_owner = acc::MemoryRestorator::kUserApplication,
      .max_hardware_jobs = 1,
      .passthrough = false};

  // Format: [<region offset relatively to some base address, region size>]
  typedef std::vector<std::tuple<uint64_t, size_t>> MemoryPartitionsOffsetBased;
  // Format: [<absolute region address, region size>]
  typedef std::vector<std::tuple<uint8_t *, size_t>> MemoryPartitions;

  PACKED_STRUCTURE {
    uint64_t original_offset; // simply the absolute address
    uint64_t original_size;
    int64_t compressed_size; // -1 if there is only one compressed partition
  }
  PartitionInfo;

  /// Instantiate a default memory restorator.
  /// If cfg_.passthrough = true, do not compress/decompress snapshots.
  MemoryRestorator(const MemoryRestoratotConfig &cfg,
                   const std::string &snapshot_filename);

  /// Instantiate a memory restorator with the global memory pool @param
  /// mem_pool for intermediate state.
  /// If cfg_.passthrough = true, do not compress/decompress snapshots.
  MemoryRestorator(const MemoryRestoratotConfig &cfg,
                   const std::string &snapshot_filename, MemoryPool *mem_pool);

  ~MemoryRestorator();

  int Init();

  // Main API.
  int MakeSnapshot(const MemoryPartitions &src_memory_partitions,
                   uint64_t base_address) const;

  int RestoreFromSnapshot(
      utils::m_mmap::Memory &mem_region, size_t mem_region_size,
      const MemoryPartitions *original_partitions = nullptr);

  // Debug API.
  bool ComparePartitions(const MemoryPartitions &p1,
                         const MemoryPartitions &p2) const;
  int DropCaches() const;

  Metrics GetMetrics() const { return metrics_; }

private:
  // All configurations is stored here.
  MemoryRestoratotConfig cfg_;

  std::string snapshot_filename_;
  MemoryPool *mem_pool_;
  std::vector<std::unique_ptr<uint8_t[]>> qpl_job_buffers_;
  mutable std::queue<int> qpl_job_idx_free_;
  bool qpl_initialized_;
  size_t page_size_;
  MemoryPartitions userfaultfd_source_partitions_;
  MemoryPartitions userfaultfd_destination_partitions_;

  Metrics metrics_;

  // For UFFDIO_CONTINUE userfaultfd serving.
  int shem_fd_;

  // QPL helpers.
  int InitQpl();
  int FreeQpl();

  int GetQplJob(qpl_job **job) const {
    if (qpl_job_idx_free_.empty())
      return -1;

    auto job_i = qpl_job_idx_free_.front();
    qpl_job_idx_free_.pop();
    *job = reinterpret_cast<qpl_job *>(qpl_job_buffers_[job_i].get());
    return job_i;
  }

  void ReturnQplJob(int job_i) const { qpl_job_idx_free_.push(job_i); }

  bool IsJobReady(int job_i, size_t *ret_size) const {
    auto job = reinterpret_cast<qpl_job *>(qpl_job_buffers_[job_i].get());
    auto status = qpl_check_job(job);
    if (status != QPL_STS_BEING_PROCESSED) {
      assert(status == QPL_STS_OK);
      *ret_size = job->total_out;
      return true;
    }
    return false;
  }

  /// Installs all pages from @param src_partitions into memory with @param
  /// dst_partitions layout of size @param size.
  int InstallAllPages(size_t size, const MemoryPartitions &dst_partitions,
                      const MemoryPartitions &src_partitions);
  void fault_handler_thread(void *arg);

  /// Apply @param partitions_layout (with relative to zero page offsets) to
  /// @param mem by adding the base address of mem to each page in
  /// partitions_layout. Store result in @param partitions_out. If @param packed
  /// is true, ignore page offsets in partitions_layout and align pages strictly
  /// one after another.
  void
  AlignMemoryPartitions(uint8_t *mem,
                        const MemoryPartitionsOffsetBased &partitions_layout,
                        MemoryPartitions &partitions_out, bool packed) const;

  // In-memory IAA/QPL functions.
  int ComputeHuffmanTables(const uint8_t *src, size_t src_size,
                           qpl_huffman_table_t *c_huffman_table) const;
  int CompressSingleChunk(qpl_huffman_table_t c_huffman_table,
                          const uint8_t *src, size_t src_size, uint8_t *dst,
                          size_t *dst_size, bool first, bool last) const;
  int DecompressSingleChunk(const uint8_t *src, size_t src_size, uint8_t *dst,
                            size_t dst_reserved_size, size_t *dst_actual_size,
                            bool blocking = true) const;

  // AUX.
  uint8_t *GetMemoryFromMemPool(size_t size);
};

} // namespace acc

#endif
