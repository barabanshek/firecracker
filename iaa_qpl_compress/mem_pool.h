#ifndef _MEM_POOL_H_
#define _MEM_POOL_H_

#include <queue>

#include "utils.h"

// Implements a simple emory pool with fixed chunks of kChunkSize.
// TODO(Nikita): add support for allocation of arbitrary memory sizes; or just
// move to DPDK's mempool.
class MemoryPool {
public:
  static constexpr size_t kChunkSize = 512 * 1024 * 1024;

  MemoryPool() : total_size_(0) {}

  int Init(size_t total_size) {
    total_size_ = total_size;
    if (total_size_ % kChunkSize)
      return -1;

    mem_buff_ = std::move(utils::mmap_allocate(total_size_, -1, false, true));
    if (mem_buff_.get() == nullptr) {
      return -1;
    }

    size_t chunk_n = total_size_ / kChunkSize;
    for (size_t i = 0; i < chunk_n; ++i)
      free_chunks.push(i);

    return 0;
  }

  bool IsInitialized() const { return total_size_ != 0; }

  uint8_t *GetMemory() {
    if (free_chunks.size() == 0)
      return nullptr;

    size_t chunk_i = free_chunks.front();
    free_chunks.pop();
    return mem_buff_.get() + chunk_i * kChunkSize;
  }

  int ReturnMemory(const uint8_t *mem) {
    size_t mem_ = reinterpret_cast<size_t>(mem);
    size_t mem_buff_begin = reinterpret_cast<size_t>(mem_buff_.get());
    if (mem_ < mem_buff_begin)
      return -1;

    size_t chunk_i = mem_ - mem_buff_begin;

    if (chunk_i % kChunkSize)
      return -1;

    if (chunk_i * kChunkSize > total_size_)
      return -1;

    free_chunks.push(chunk_i);
    return 0;
  }

  size_t getMaxAllocationSize() const { return kChunkSize; }

private:
  size_t total_size_;
  std::unique_ptr<uint8_t, utils::MMapDeleter> mem_buff_;
  std::queue<size_t> free_chunks;
};

#endif
