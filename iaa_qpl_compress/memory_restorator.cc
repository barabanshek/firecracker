#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <thread>

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "memory_restorator.h"

namespace acc {

static const std::string kPartitionInfoFileNameSuffix = "partitions";
static const std::string kSnapshotFileNameSuffix = "snapshot";

MemoryRestorator::MemoryRestorator(const MemoryRestoratotConfig &cfg,
                                   const std::string &snapshot_filename)
    : cfg_(cfg), snapshot_filename_(snapshot_filename), mem_pool_(nullptr),
      qpl_initialized_(false), shem_fd_(-1) {
  page_size_ = static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
  assert(page_size_ == sys::kPageSize);
};

MemoryRestorator::MemoryRestorator(const MemoryRestoratotConfig &cfg,
                                   const std::string &snapshot_filename,
                                   MemoryPool *mem_pool)
    : cfg_(cfg), snapshot_filename_(snapshot_filename), mem_pool_(mem_pool),
      qpl_initialized_(false), shem_fd_(-1) {
  page_size_ = static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
  assert(page_size_ == sys::kPageSize);
}

MemoryRestorator::~MemoryRestorator() {
  if (qpl_initialized_)
    FreeQpl();
}

int MemoryRestorator::InitQpl() {
  uint32_t job_size = 0;
  qpl_status status = qpl_get_job_size(cfg_.execution_path, &job_size);
  if (status != QPL_STS_OK) {
    return -1;
  }

  for (int i = 0; i < cfg_.max_hardware_jobs; ++i) {
    std::unique_ptr<uint8_t[]> job_buffer;
    job_buffer = std::make_unique<uint8_t[]>(job_size);
    auto job = reinterpret_cast<qpl_job *>(job_buffer.get());
    qpl_status status = qpl_init_job(cfg_.execution_path, job);
    if (status != QPL_STS_OK) {
      return -1;
    }
    qpl_job_buffers_.push_back(std::move(job_buffer));
    qpl_job_idx_free_.push(i);
  }

  return 0;
}

int MemoryRestorator::FreeQpl() {
  for (auto &job_buffer : qpl_job_buffers_) {
    auto job = reinterpret_cast<qpl_job *>(job_buffer.get());
    qpl_status status = qpl_fini_job(job);
    if (status != QPL_STS_OK) {
      return -1;
    }
  }

  return 0;
}

int MemoryRestorator::Init() {
  if (InitQpl()) {
    RLOG(0) << "Failed to init QPL." << std::endl;
    return -1;
  }

  qpl_initialized_ = true;
  return 0;
}

int MemoryRestorator::ComputeHuffmanTables(
    const uint8_t *src, size_t src_size,
    qpl_huffman_table_t *c_huffman_table) const {
  // Create Huffman tables.
  qpl_status status =
      qpl_deflate_huffman_table_create(combined_table_type, cfg_.execution_path,
                                       DEFAULT_ALLOCATOR_C, c_huffman_table);
  if (status != QPL_STS_OK) {
    RLOG(0) << "Failed to allocate Huffman tables" << std::endl;
    return -1;
  }

  // Gather statistics.
  qpl_histogram histogram{};
  status = qpl_gather_deflate_statistics(const_cast<uint8_t *>(src), src_size,
                                         &histogram, qpl_default_level,
                                         cfg_.execution_path);
  if (status != QPL_STS_OK) {
    RLOG(0) << "Failed to gather statistics." << std::endl;
    qpl_huffman_table_destroy(*c_huffman_table);
    return -1;
  }

  // Populate Huffman tabes with the statistics.
  status = qpl_huffman_table_init_with_histogram(*c_huffman_table, &histogram);
  if (status != QPL_STS_OK) {
    RLOG(0) << "Failed to populate the Huffman tabels." << std::endl;
    qpl_huffman_table_destroy(*c_huffman_table);
    return -1;
  }

  return 0;
}

int MemoryRestorator::CompressSingleChunk(qpl_huffman_table_t c_huffman_table,
                                          const uint8_t *src, size_t src_size,
                                          uint8_t *dst, size_t *dst_size,
                                          bool first, bool last) const {
  if (!qpl_initialized_) {
    RLOG(0) << "QPL is not initialized!" << std::endl;
    return -1;
  }
  qpl_job *job;
  auto job_id = GetQplJob(&job);
  assert(job_id != -1);
  assert(job != nullptr);

  // Compress.
  job->op = qpl_op_compress;
  job->level = qpl_default_level;
  job->next_in_ptr = const_cast<uint8_t *>(src);
  job->available_in = src_size;
  job->next_out_ptr = dst;
  job->available_out = src_size - 1;
  job->flags = QPL_FLAG_OMIT_VERIFY | QPL_FLAG_FIRST | QPL_FLAG_LAST;
  // if (first)
  //   job->flags |= QPL_FLAG_FIRST;
  // if (last)
  //   job->flags |= QPL_FLAG_LAST;

  // If Huffman tables are not provided - do dynamic Huffman.
  if (c_huffman_table == nullptr)
    job->flags |= QPL_FLAG_DYNAMIC_HUFFMAN;
  else {
    // job->flags |= QPL_FLAG_CANNED_MODE;
    job->huffman_table = c_huffman_table;
  }

  // Execute compression operation.
  qpl_status status = qpl_execute_job(job);
  if (status != QPL_STS_OK) {
    RLOG(0) << "An error " << status << " acquired during compression."
            << std::endl;
    return -1;
  }

  // Return size.
  *dst_size = job->total_out;

  ReturnQplJob(job_id);
  return 0;
}

int MemoryRestorator::DecompressSingleChunk(const uint8_t *src, size_t src_size,
                                            uint8_t *dst,
                                            size_t dst_reserved_size,
                                            size_t *dst_actual_size,
                                            bool blocking) const {
  if (!qpl_initialized_) {
    RLOG(0) << "QPL is not initialized!" << std::endl;
    return -1;
  }
  qpl_job *job;
  auto job_id = GetQplJob(&job);
  assert(job_id != -1);
  assert(job != nullptr);

  // Decompress.
  job->op = qpl_op_decompress;
  job->next_in_ptr = const_cast<uint8_t *>(src);
  job->next_out_ptr = dst;
  job->available_in = src_size;
  job->available_out = dst_reserved_size;
  job->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST;
  // job->flags |= QPL_FLAG_CANNED_MODE;
  // job->huffman_table = c_huffman_table;

  // Execute decompression operation.
  if (blocking) {
    qpl_status status = qpl_execute_job(job);
    if (status != QPL_STS_OK) {
      RLOG(0) << "Error while decompression occurred: " << status << std::endl;
      return -1;
    }

    // Return size.
    *dst_actual_size = job->total_out;
    ReturnQplJob(job_id);
    return 0;
  } else {
    qpl_status status = qpl_submit_job(job);
    if (status != QPL_STS_OK) {
      RLOG(0) << "Error while submitting non blocking decompression occured: "
              << status << std::endl;
      return -1;
    }
    return job_id;
  }
}

int MemoryRestorator::MakeSnapshot(
    const MemoryPartitions &src_memory_partitions,
    uint64_t base_address) const {
  size_t src_total_size = 0;
  for (auto const &[p_ptr, p_size] : src_memory_partitions)
    src_total_size += p_size;

  auto src = utils::malloc_allocate(src_total_size);
  std::vector<size_t> dst_partition_sizes;
  auto dst = utils::malloc_allocate(src_total_size);
  if (src.get() == nullptr || dst.get() == nullptr) {
    RLOG(0) << "Failed to allocate memory." << std::endl;
    return -1;
  }

  // Gather all chunks together.
  // TODO(Nikita): this can potentially be optimized-out (especially for
  // kHandleAsScatteredPartitions), but for now - off the critical path...
  size_t offset = 0;
  for (auto const &[p_ptr, p_size] : src_memory_partitions) {
    std::memmove(src.get() + offset, p_ptr, p_size);
    offset += p_size;
  }

  // Handle path.
  size_t dst_compressed_total_size = 0;
  if (!cfg_.passthrough) {
    if (cfg_.partition_hanlding_path == kHandleAsSinglePartition) {
      // Compress all chunks together.
      int ret =
          CompressSingleChunk(nullptr, src.get(), src_total_size, dst.get(),
                              &dst_compressed_total_size, true, true);
      if (ret) {
        RLOG(0) << "An error acquired during compression." << std::endl;
        return -1;
      }
    } else {
      // First, compute Huffman tables.
      qpl_huffman_table_t c_huffman_table = nullptr;
      if (cfg_.scattered_partition_handling_path ==
          kDoStaticHuffmanForScatteredPartitions) {
        if (ComputeHuffmanTables(src.get(), src_total_size, &c_huffman_table)) {
          RLOG(0) << "An error acquired during Huffman table compute."
                  << std::endl;
          return -1;
        }
      }

      // Then use Huffman tables to do scattered compression.
      size_t compressed_size = 0;
      size_t p_id = 0;
      for (auto const &[p_ptr, p_size] : src_memory_partitions) {
        if (CompressSingleChunk(c_huffman_table, p_ptr, p_size,
                                dst.get() + dst_compressed_total_size,
                                &compressed_size, p_id == 0,
                                p_id == src_memory_partitions.size() - 1)) {
          RLOG(0) << "An error acquired during compression." << std::endl;
          return -1;
        }
        dst_partition_sizes.push_back(compressed_size);
        dst_compressed_total_size += compressed_size;
        ++p_id;
      }
    }
  } else {
    dst_compressed_total_size = src_total_size;
  }

  // Dump partition info to a file.
  auto partition_info_filename =
      snapshot_filename_ + "." + kPartitionInfoFileNameSuffix;
  int partition_info_fd =
      open(partition_info_filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0x666);
  if (partition_info_fd == -1) {
    RLOG(0) << "Error during file open." << std::endl;
    return -1;
  }

  // Write number of partitions.
  uint64_t p_number = static_cast<uint64_t>(src_memory_partitions.size());
  if (write(partition_info_fd, &p_number, sizeof(p_number)) !=
      sizeof(p_number)) {
    RLOG(0) << "Error during write." << std::endl;
    close(partition_info_fd);
    return -1;
  }

  // Write partition info.
  uint64_t src_memory_partitions_begin = base_address;
  // reinterpret_cast<uint64_t>(std::get<0>(src_memory_partitions.front())); //
  // TODO(Nikita): BUG here!!! This is not going to work.
  size_t i = 0;
  for (auto const &[p_ptr, p_size] : src_memory_partitions) {
    PartitionInfo p_info;
    p_info.original_offset =
        reinterpret_cast<uint64_t>(p_ptr) - src_memory_partitions_begin;
    p_info.original_size = static_cast<uint64_t>(p_size);
    if (cfg_.partition_hanlding_path == kHandleAsSinglePartition ||
        cfg_.passthrough)
      p_info.compressed_size = -1;
    else
      p_info.compressed_size = dst_partition_sizes[i];
    if (write(partition_info_fd, &p_info, sizeof(PartitionInfo)) !=
        sizeof(PartitionInfo)) {
      RLOG(0) << "Error during write." << std::endl;
      close(partition_info_fd);
      return -1;
    }
    ++i;
  }
  fsync(partition_info_fd);
  close(partition_info_fd);

  // Dump partitions.
  auto snapshot_filename = snapshot_filename_ + "." + kSnapshotFileNameSuffix;
  int snapshot_fd =
      open(snapshot_filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0x666);
  if (snapshot_fd == -1) {
    RLOG(0) << "Error during file open." << std::endl;
    return -1;
  }

  if (cfg_.partition_hanlding_path == kHandleAsSinglePartition) {
    if (write(snapshot_fd, cfg_.passthrough ? src.get() : dst.get(),
              dst_compressed_total_size) !=
        static_cast<ssize_t>(dst_compressed_total_size)) {
      RLOG(0) << "Error during write." << std::endl;
      close(snapshot_fd);
      return -1;
    }
  } else {
    // TODO(Nikita): remove duplicated code.
    if (write(snapshot_fd, cfg_.passthrough ? src.get() : dst.get(),
              dst_compressed_total_size) !=
        static_cast<ssize_t>(dst_compressed_total_size)) {
      RLOG(0) << "Error during write." << std::endl;
      close(snapshot_fd);
      return -1;
    }
  }
  fsync(snapshot_fd);
  close(snapshot_fd);

  RLOG(1) << "Snapshot created:" << std::endl;
  RLOG(1) << "    name: " << snapshot_filename_ << std::endl;
  RLOG(1) << "    # of partitions: " << p_number << std::endl;
  RLOG(1) << "    original size (B): " << src_total_size << std::endl;
  RLOG(1) << "    compressed size (B): " << dst_compressed_total_size << "(x"
          << 1.0 * src_total_size / dst_compressed_total_size << ")"
          << std::endl;

  return 0;
}

void MemoryRestorator::AlignMemoryPartitions(
    uint8_t *mem, const MemoryPartitionsOffsetBased &partitions_layout,
    MemoryPartitions &partitions_out, bool packed) const {
  if (packed) {
    size_t offset = 0;
    for (auto const &[p_ptr, p_size] : partitions_layout) {
      partitions_out.push_back(std::make_tuple(mem + offset, p_size));
      offset += p_size;
    }
  } else {
    for (auto const &[p_ptr, p_size] : partitions_layout) {
      partitions_out.push_back(std::make_tuple(mem + p_ptr, p_size));
    }
  }
}

int MemoryRestorator::RestoreFromSnapshot(
    std::unique_ptr<uint8_t, utils::MMapDeleter> &mem_region,
    size_t mem_region_size, const MemoryPartitions *original_partitions) {
  // To profile, as the performance of this is the key enabler of this idea.
  utils::TimeScope time_begin;

  std::unique_ptr<uint8_t, utils::MMapDeleter> dst_mem_region;
  if (cfg_.restored_memory_owner == kUserApplication) {
    if (cfg_.sigle_partition_handling_path == kHandleWithUffdioContinue) {
      RLOG(0) << "Memory owned by application can not be restored with "
                 "UFFDIO_CONTINUE"
              << std::endl;
      return -1;
    }
    // Since user application own the memory, check that it is allocated.
    if (mem_region.get() == nullptr) {
      RLOG(0) << "NULL memory is received from the owning application."
              << std::endl;
      return -1;
    }

    dst_mem_region = std::move(mem_region);
  } else {
    // Check that the pointer is null, so we can allocate memory here.
    if (mem_region.get() != nullptr) {
      RLOG(0) << "Memory is already allocated" << std::endl;
      return -1;
    }

    if (cfg_.passthrough) {
      RLOG(0) << "In passthrough mode, only application can own the memory."
              << std::endl;
      return -1;
    }

    // Allocate destination memory.
    if (cfg_.partition_hanlding_path == kHandleAsScatteredPartitions ||
        cfg_.sigle_partition_handling_path == kHandleWithUffdioCopy)
      dst_mem_region = utils::mmap_allocate(mem_region_size);
    else
      dst_mem_region = utils::shem_allocate(mem_region_size, &shem_fd_);

    if (dst_mem_region.get() == nullptr) {
      RLOG(0) << "Failed to allocate destination memory region." << std::endl;
      return -1;
    }
  }

  RLOG(1) << "Mmap destination memory, took: "
          << time_begin.GetScopeTimeStamp<std::chrono::microseconds>() << "us "
          << std::endl;

  // Read partition info.
  auto partition_info_filename =
      snapshot_filename_ + "." + kPartitionInfoFileNameSuffix;
  int partition_info_fd = open(partition_info_filename.c_str(), O_RDWR);
  if (partition_info_fd == -1) {
    RLOG(0) << "Error during partition file open: " << partition_info_filename
            << std::endl;
    return -1;
  }

  uint64_t p_number = 0;
  if (read(partition_info_fd, &p_number, sizeof(p_number)) !=
      sizeof(p_number)) {
    RLOG(0) << "Error during read." << std::endl;
    close(partition_info_fd);
    return -1;
  }
  RLOG(1) << "Number of partitions: " << p_number << std::endl;

  MemoryPartitionsOffsetBased snapshot_memory_partitions;
  std::vector<std::tuple<uint64_t, uint64_t>> src_offset_size; // <offset, size>
  size_t total_decompress_size = 0;
  size_t p_offset = 0;
  for (size_t i = 0; i < p_number; ++i) {
    PartitionInfo p_info;
    if (read(partition_info_fd, &p_info, sizeof(PartitionInfo)) !=
        sizeof(PartitionInfo)) {
      RLOG(0) << "Error during read." << std::endl;
      close(partition_info_fd);
      return -1;
    }
    snapshot_memory_partitions.push_back(
        std::make_tuple(p_info.original_offset, p_info.original_size));
    if (p_info.compressed_size != -1) {
      src_offset_size.push_back(
          std::make_tuple(p_offset, p_info.compressed_size));
      p_offset += p_info.compressed_size;
    }
    total_decompress_size += p_info.original_size;
    RLOG(2) << "    " << i << ": " << std::hex << p_info.original_offset << ", "
            << std::dec << p_info.original_size << ", "
            << p_info.compressed_size << std::endl;
  }
  close(partition_info_fd);

  MemoryPartitions dst_memory_partitions;
  AlignMemoryPartitions(dst_mem_region.get(), snapshot_memory_partitions,
                        dst_memory_partitions, false);

  RLOG(1) << "Get partition info, took: "
          << time_begin.GetScopeTimeStamp<std::chrono::microseconds>() << "us "
          << std::endl;

  if (src_offset_size.size() == 0) {
    if (!cfg_.passthrough)
      assert(cfg_.partition_hanlding_path == kHandleAsSinglePartition);
  } else {
    assert(src_offset_size.size() == p_number);
    assert(cfg_.partition_hanlding_path == kHandleAsScatteredPartitions);
  }

  // Open and read/pre-fetch (if needed) the snapshot file.
  auto snapshot_filename = snapshot_filename_ + "." + kSnapshotFileNameSuffix;
  int snapshot_fd = -1;
  size_t snapshot_file_size = 0;
  std::unique_ptr<uint8_t, utils::MMapDeleter> src;
  if (!cfg_.passthrough) {
    // Here, we will do the decompression, so open and mmap without any
    // pre-faulting and/or pre-fetching.
    snapshot_fd = open(snapshot_filename.c_str(), O_RDWR);
    if (snapshot_fd == -1) {
      RLOG(0) << "Error during file open." << std::endl;
      return -1;
    }
    snapshot_file_size = static_cast<size_t>(lseek(snapshot_fd, 0L, SEEK_END));
    lseek(snapshot_fd, 0L, SEEK_SET);

    // Mmap file.
    src = utils::mmap_allocate(snapshot_file_size, snapshot_fd, false, false);
    if (src.get() == nullptr) {
      RLOG(0) << "Failed to mmap file." << std::endl;
      close(snapshot_fd);
      return -1;
    }
  } else {
    // No decompression will be done (this is fop reap), just read it with
    // O_DIRECT.
    snapshot_fd = open(snapshot_filename.c_str(), O_RDWR | O_DIRECT);
    if (snapshot_fd == -1) {
      RLOG(0) << "Error during file open." << std::endl;
      return -1;
    }
    snapshot_file_size = static_cast<size_t>(lseek(snapshot_fd, 0L, SEEK_END));
    lseek(snapshot_fd, 0L, SEEK_SET);

    // Fetch.
    src = utils::mmap_allocate(snapshot_file_size, -1, false, false);
    if (read(snapshot_fd, src.get(), snapshot_file_size) !=
        snapshot_file_size) {
      RLOG(0) << "Failed to pre-fetch snapshot file." << std::endl;
      return -1;
    }
  }

  RLOG(1) << "Mmap (and fetch if passthough) snapshot file, took: "
          << time_begin.GetScopeTimeStamp<std::chrono::microseconds>() << " us"
          << std::endl;

  if (cfg_.partition_hanlding_path == kHandleAsSinglePartition) {
    // We need to do the following:
    //  - allocate intermediate buffer for decompression;
    //  - decompress;
    //  - install partitions from the intermediate buffer into the application
    //  memory according to snapshot_memory_partitions information.

    // Get memory for the decompression buffers.
    bool need_to_explicitly_release_decompressed_memory =
        false; // TODO(Nikita): avoid this!

    std::unique_ptr<uint8_t, utils::MMapDeleter> decompressed_memory;
    MemoryPartitions memory_partitions_to_install;
    if (!cfg_.passthrough) {
      // Do decompression.
      if (cfg_.sigle_partition_handling_path == kHandleWithUffdioCopy) {
        // We are going to userfaultfd COPY pages from this memory into the
        // destination, so just allocate it.
        if (mem_pool_ != nullptr) {
          // If we have pre-allocated memory pool to use, go for it.
          RLOG(1) << "Allocating decompression buffer from the memory pool"
                  << std::endl;
          auto m_buff = GetMemoryFromMemPool(total_decompress_size);
          decompressed_memory.reset(m_buff);
          need_to_explicitly_release_decompressed_memory = true;
        } else {
          RLOG(1) << "Allocating local private decompression buffer"
                  << std::endl;
          decompressed_memory =
              utils::mmap_allocate(total_decompress_size, -1, true, true);
        }
      } else {
        // We are going to use userfaultfd's CONTINUE mode, this memory must be
        // shared with the destination buffer via shem.
        RLOG(1) << "Allocating shem backed shared decompression buffer"
                << std::endl;
        decompressed_memory =
            utils::shem_allocate(total_decompress_size, &shem_fd_);
      }
      if (decompressed_memory.get() == nullptr) {
        RLOG(0)
            << "Failed to allocate memory for the decompression, requested: "
            << total_decompress_size << " B" << std::endl;
        close(snapshot_fd);
        return -1;
      }

      RLOG(1) << "Mmap decompression buffer, took: "
              << time_begin.GetScopeTimeStamp<std::chrono::microseconds>()
              << " us" << std::endl;

      // Decompress.
      size_t actual_decompress_size = 0;
      if (DecompressSingleChunk(
              src.get(), snapshot_file_size, decompressed_memory.get(),
              total_decompress_size, &actual_decompress_size) == -1) {
        RLOG(0) << "Error during decompression." << std::endl;
        close(snapshot_fd);
        return -1;
      }
      if (actual_decompress_size != total_decompress_size) {
        RLOG(0) << "Decompressed data size missmatch." << std::endl;
        close(snapshot_fd);
        return -1;
      }
      assert(actual_decompress_size % page_size_ == 0);

      AlignMemoryPartitions(decompressed_memory.get(),
                            snapshot_memory_partitions,
                            memory_partitions_to_install, true);

      RLOG(1) << "Decompress, took: "
              << time_begin.GetScopeTimeStamp<std::chrono::microseconds>()
              << " us" << std::endl;

      // If in debug, compare partitions.
      if (original_partitions != nullptr) {
        if (ComparePartitions(*original_partitions,
                              memory_partitions_to_install) == false) {
          RLOG(0) << "Missmatch in decompressed partitions." << std::endl;
        }
        RLOG(0) << "Decompressed partitions match original memory."
                << std::endl;
      }
    } else {
      AlignMemoryPartitions(src.get(), snapshot_memory_partitions,
                            memory_partitions_to_install, true);
    }

    // Install pages.
    if (InstallAllPages(mem_region_size, dst_memory_partitions,
                        memory_partitions_to_install)) {
      RLOG(0) << "Failed to install pages." << std::endl;
      close(snapshot_fd);
      return -1;
    }

    RLOG(1) << "Install pages, took: "
            << time_begin.GetScopeTimeStamp<std::chrono::microseconds>()
            << " us" << std::endl;

    // Release some memory.
    // TODO(Nikita): do it automatically!
    if (mem_pool_ != nullptr &&
        need_to_explicitly_release_decompressed_memory) {
      if (mem_pool_->ReturnMemory(decompressed_memory.get())) {
        RLOG(1) << "Failed to return memory to mempool.";
        close(snapshot_fd);
        return -1;
      }
    }
  } else {
    if (cfg_.passthrough) {
      RLOG(0) << "Only kHandleAsSinglePartition is allowed in passthrough mode."
              << std::endl;
      return -1;
    }

    // The only thing we need to do is to decompress partitions into the
    // application memory according to snapshot_memory_partitions information.
    MemoryPartitions dst_memory_partitions;
    AlignMemoryPartitions(dst_mem_region.get(), snapshot_memory_partitions,
                          dst_memory_partitions, false);

    assert(dst_memory_partitions.size() == src_offset_size.size());

    if (cfg_.max_hardware_jobs == 1) {
      // Handle as a single blocking job.
      size_t i = 0;
      for (auto const &[dst_ptr, dst_size] : dst_memory_partitions) {
        size_t actual_decompress_size = 0;
        if (DecompressSingleChunk(src.get() + std::get<0>(src_offset_size[i]),
                                  std::get<1>(src_offset_size[i]), dst_ptr,
                                  dst_size, &actual_decompress_size,
                                  true) == -1) {
          RLOG(0) << "Error during decompression of partition #" << i
                  << std::endl;
          close(snapshot_fd);
          return -1;
        }
        assert(actual_decompress_size == dst_size);
        ++i;
      }
    } else {
      // Handle concurrently.
      size_t partition_i = 0;
      std::map<int, size_t> job_id_2_dst_id;
      for (auto const &[dst_ptr, dst_size] : dst_memory_partitions) {
        while (qpl_job_idx_free_.empty()) {
          // Need to reclaim.
          for (auto const &[job_id, job_partition_i] : job_id_2_dst_id) {
            size_t ret_size = 0;
            if (IsJobReady(job_id, &ret_size)) {
              // Check output and reclaim.
              assert(ret_size ==
                     std::get<1>(dst_memory_partitions[job_partition_i]));
              ReturnQplJob(job_id);
              job_id_2_dst_id.erase(job_id);
              break;
            }
          }
        }

        auto job_id = DecompressSingleChunk(
            src.get() + std::get<0>(src_offset_size[partition_i]),
            std::get<1>(src_offset_size[partition_i]), dst_ptr, dst_size,
            nullptr, false);
        if (job_id == -1) {
          RLOG(0) << "Error during decompression of partition #" << partition_i
                  << std::endl;
          close(snapshot_fd);
          return -1;
        }

        job_id_2_dst_id[job_id] = partition_i;
        ++partition_i;
      }

      // Drain all remaining jobs.
      while (job_id_2_dst_id.size() != 0) {
        for (auto const &[job_id, job_partition_i] : job_id_2_dst_id) {
          size_t ret_size = 0;
          if (IsJobReady(job_id, &ret_size)) {
            // Check output and reclaim.
            assert(ret_size ==
                   std::get<1>(dst_memory_partitions[job_partition_i]));
            ReturnQplJob(job_id);
            job_id_2_dst_id.erase(job_id);
            break;
          }
        }
      }
    }

    RLOG(1) << "Decompress, took: "
            << time_begin.GetScopeTimeStamp<std::chrono::microseconds>()
            << " us" << std::endl;
  }

  RLOG(1) << "Memory restoration, took: "
          << time_begin.GetAbsoluteTimeStamp<std::chrono::microseconds>()
          << " us" << std::endl;
  mem_region = std::move(dst_mem_region);
  close(snapshot_fd);
  return 0;
}

void MemoryRestorator::fault_handler_thread(void *arg) {
  auto uffd = (long)arg;
  pollfd pollfd;
  uffdio_copy uffdio_copy;
  uffdio_zeropage uffdio_zero_page;
  uffdio_continue uffdio_continue;
  uffd_msg msg;

  while (true) {
    pollfd.fd = uffd;
    pollfd.events = POLLIN;
    int nready = poll(&pollfd, 1, -1);
    if (nready == -1) {
      RLOG(0) << "uffd poll error." << std::endl;
      return;
    }

    ssize_t nread = read(uffd, &msg, sizeof(msg));
    if (nread == 0) {
      RLOG(0) << "EOF on userfaultfd." << std::endl;
      return;
    }

    if (nread == -1) {
      RLOG(0) << "Failed to read on uffd." << std::endl;
      return;
    }

    if (msg.event != UFFD_EVENT_PAGEFAULT) {
      RLOG(0) << "Unexpected event on userfaultfd." << std::endl;
      return;
    }

    assert(msg.arg.pagefault.address % page_size_ == 0);

    //
    // std::cout << "INSTALL: by: " << msg.arg.pagefault.address << ", size= "
    //           << std::get<1>(userfaultfd_partitions_[userfaultfd_it_])
    //           << ", first_word= "
    //           << *(uint64_t *)(std::get<0>(
    //                  userfaultfd_partitions_[userfaultfd_it_]))
    //           << std::endl;

    // Handle depending on the path.
    if (cfg_.sigle_partition_handling_path == kHandleWithUffdioCopy) {
      uffdio_copy.src = reinterpret_cast<uint64_t>(
          std::get<0>(userfaultfd_partitions_[userfaultfd_it_]));
      uffdio_copy.dst = msg.arg.pagefault.address;
      uffdio_copy.len = std::get<1>(userfaultfd_partitions_[userfaultfd_it_]);
      uffdio_copy.mode = 0;
      uffdio_copy.copy = 0;
      if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
        RLOG(0) << "ioctl-UFFDIO_COPY error." << std::endl;
        return;
      }
    } else {
      uffdio_continue.range.start = msg.arg.pagefault.address;
      uffdio_continue.range.len =
          std::get<1>(userfaultfd_partitions_[userfaultfd_it_]);
      uffdio_continue.mode = 0;
      if (ioctl(uffd, UFFDIO_CONTINUE, &uffdio_continue) == -1) {
        RLOG(0) << "ioctl-UFFDIO_CONTINUE error." << std::endl;
        return;
      }
    }

    if (userfaultfd_it_ == userfaultfd_total_pages_to_install_ - 1) {
      RLOG(1) << "Terminating userfaultfd thread." << std::endl;
      return;
    }

    ++userfaultfd_it_;
  }
}

int MemoryRestorator::InstallAllPages(
    size_t size, const MemoryPartitions &partitions,
    const MemoryPartitions &decompressed_partitions) {
  auto mem = std::get<0>(partitions.front());

  // Create and enable userfaultfd object.
  long uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
  if (uffd == -1) {
    RLOG(0) << "Failed to create and enable userfaultfd object." << std::endl;
    return -1;
  }

  uffdio_api uffdio_api;
  uffdio_register uffdio_register;

  uffdio_api.api = UFFD_API;
  uffdio_api.features = 0;
  if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
    RLOG(0) << "ioctl-UFFDIO_API." << std::endl;
    return -1;
  }

  RLOG(2) << "uffdio_api.features: " << std::hex << uffdio_api.features
          << std::dec << ", UFFD_FEATURE_MINOR_HUGETLBFS: "
          << (uffdio_api.features & UFFD_FEATURE_MINOR_HUGETLBFS)
          << ", UFFD_FEATURE_MINOR_SHMEM: "
          << (uffdio_api.features & UFFD_FEATURE_MINOR_SHMEM) << std::endl;

  // Register our memory with userfaultfd.
  uffdio_register.range.start = reinterpret_cast<uint64_t>(mem);
  uffdio_register.range.len = size;
  uffdio_register.mode =
      cfg_.sigle_partition_handling_path == kHandleWithUffdioCopy
          ? UFFDIO_REGISTER_MODE_MISSING
          : UFFDIO_REGISTER_MODE_MINOR;
  if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
    RLOG(0) << "Failed to register memory with userfaultfd." << std::endl;
    return -1;
  }

  // Create a thread that will process the userfaultfd events.
  auto t =
      std::thread(&MemoryRestorator::fault_handler_thread, this, (void *)uffd);

  // Install all partitions via userfaultfd by touching them.
  RLOG(1) << "Total partitions to install: " << partitions.size() << std::endl;
  userfaultfd_total_pages_to_install_ = partitions.size();
  userfaultfd_partitions_ = decompressed_partitions;
  userfaultfd_it_ = 0;
  for (auto const &[p_ptr, p_size] : partitions) {
    assert(p_size % page_size_ == 0);
    volatile uint8_t *partition_addr = p_ptr;
    *partition_addr;
  }
  t.join();

  assert(userfaultfd_it_ == userfaultfd_total_pages_to_install_ - 1);
  RLOG(1) << "Installed partitions: " << userfaultfd_it_ + 1 << std::endl;

  // Unregister memory from userfaultfd to allow the VM to continue with its
  // native page fault handling on fresh pages.
  if (ioctl(uffd, UFFDIO_UNREGISTER, &uffdio_register) == -1) {
    RLOG(0) << "Failed to unregister memory with userfaultfd." << std::endl;
    return -1;
  }

  return 0;
}

bool MemoryRestorator::ComparePartitions(const MemoryPartitions &p1,
                                         const MemoryPartitions &p2) const {
  if (p1.size() != p2.size()) {
    RLOG(0) << "ComparePartitions: missmatch in total number of partitions."
            << std::endl;
    return false;
  }

  for (size_t i = 0; i < p1.size(); ++i) {
    if (std::get<1>(p1[i]) != std::get<1>(p2[i])) {
      RLOG(0) << "ComparePartitions: missmatch in size of partition: " << i
              << std::endl;
      return false;
    }
    if (memcmp(std::get<0>(p1[i]), std::get<0>(p2[i]), std::get<1>(p1[i]))) {
      RLOG(0) << "ComparePartitions: missmatch in content of partition: " << i
              << std::endl;
      return false;
    }
  }

  return true;
}

uint8_t *MemoryRestorator::GetMemoryFromMemPool(size_t size) {
  if (mem_pool_ == nullptr)
    return nullptr;

  if (size > mem_pool_->getMaxAllocationSize()) {
    RLOG(0) << "Failed to allocate memory for the decompression, "
               "unsupported chunk size in mempool."
            << std::endl;
    return nullptr;
  }
  auto m_buff = mem_pool_->GetMemory();
  if (m_buff == nullptr) {
    RLOG(0) << "Failed to allocate memory for the decompression." << std::endl;
    return nullptr;
  }

  return m_buff;
}

int MemoryRestorator::DropCaches() const {
  std::vector<std::string> filenames = {
      snapshot_filename_ + "." + kPartitionInfoFileNameSuffix,
      snapshot_filename_ + "." + kSnapshotFileNameSuffix};
  for (auto const &filename : filenames) {
    if (system((std::string("sudo dd of=") + filename +
                " oflag=nocache conv=notrunc,fdatasync count=0")
                   .c_str()))
      return -1;
  }
  return 0;
}

} // namespace acc
