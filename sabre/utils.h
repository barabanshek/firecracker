#ifndef _UTILS_H_
#define _UTILS_H_

#include <chrono>
#include <cstring>
#include <memory>
#include <string>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <iostream>

// TODO(Nikita): move somewhere else
namespace sys {

// TODO (Nikita): read it from somewhere.
static size_t kPageSize = 4096;
static size_t kHugePageSize = 2 * 1024 * 1024;
} // namespace sys

namespace utils {

// AUX stuff.
#define PACKED_STRUCTURE typedef struct __attribute__((__packed__))

// Nice constants.
static constexpr uint64_t kkB = 1024;
static constexpr uint64_t kMB = 1024 * 1024;

// Time measurement tool.
class TimeScope {
public:
  TimeScope() {
    start_tick_ = std::chrono::high_resolution_clock::now();
    last_tick_ = start_tick_;
  }

  template <class T> auto GetAbsoluteTimeStamp() {
    auto now_tick = std::chrono::high_resolution_clock::now();
    auto delta_tick = std::chrono::duration_cast<T>(now_tick - start_tick_);
    return delta_tick.count();
  }

  template <class T> auto GetScopeTimeStamp() {
    auto now_tick = std::chrono::high_resolution_clock::now();
    auto delta_tick = std::chrono::duration_cast<T>(now_tick - last_tick_);
    last_tick_ = now_tick;
    return delta_tick.count();
  }

private:
  std::chrono::time_point<std::chrono::high_resolution_clock> start_tick_;
  std::chrono::time_point<std::chrono::high_resolution_clock> last_tick_;
};

// Memory allocators.
// Just nullptr memory.
#define nil Memory(nullptr)

// Allocator based on mmap.
namespace m_mmap {
class Deleter {
public:
  // Gets called when the owning unique_ptr is released.
  void operator()(void *ptr) const {
    if (ptr != nullptr) {
      int res = munmap(ptr, size_);
      std::cout << " DELETED: " << res << " ptr: " << std::hex << (void *)(ptr)
                << std::dec << " size: " << size_ << " \n";
    }
    if (fd_ != -1)
      close(fd_);
  }

  // Set parameters of this memory.
  void set_size(size_t size) { size_ = size; }
  void set_fd(int fd) { fd_ = fd; }

private:
  size_t size_ = 0;
  int fd_ = -1;
};

// Main type for this memory.
typedef std::unique_ptr<uint8_t, Deleter> Memory;

/// Allocate @param size of memory using mmap; if @param fd is set, allocate
/// from file; if @param huge_pages is set, use huge pages; prefault memory if
/// @param prefault is true.
/// Returns a unique_ptr managed memory region with deallocation logic specified
/// in class Deleter.
static Memory allocate(size_t size, int fd = -1, bool huge_pages = false,
                       bool prefault = false) {
  int flags = 0;
  if (fd > 0) {
    flags = prefault ? MAP_SHARED | MAP_POPULATE : MAP_SHARED;
  } else {
    flags = prefault ? MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE
                     : MAP_PRIVATE | MAP_ANONYMOUS;
  }
  if (huge_pages)
    flags |= MAP_HUGETLB;

  uint8_t *ptr = reinterpret_cast<uint8_t *>(
      mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, fd > 0 ? fd : -1, 0));
  if (ptr == MAP_FAILED)
    return nullptr;

  Memory unique_ptr(ptr);
  if (huge_pages) {
    // Round-up to a multiple of the huge page size.
    if (size % sys::kHugePageSize)
      size = (size / sys::kHugePageSize + 1) * sys::kHugePageSize;
  }
  unique_ptr.get_deleter().set_size(size);
  return unique_ptr;
}

/// Allocate memory of size @param size backed by shem at @param fd.
static Memory shem_allocate(size_t size, int *fd) {
  bool fresh = false;
  if (*fd == -1) {
    const char *name = "shm.file";
    *fd = shm_open(name, O_RDWR | O_CREAT, 0644);
    if (*fd == -1) {
      return Memory(nullptr);
    }
    if (ftruncate(*fd, size)) {
      return Memory(nullptr);
    }
    fresh = true;
  }
  uint8_t *ptr = reinterpret_cast<uint8_t *>(
      mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0));
  if (ptr == MAP_FAILED)
    return nullptr;

  Memory unique_ptr(ptr);
  unique_ptr.get_deleter().set_size(size);
  if (fresh) {
    unique_ptr.get_deleter().set_fd(*fd);
  }
  return unique_ptr;
}

} // namespace m_mmap

// Allocator based on malloc.
namespace m_malloc {
class Deleter {
public:
  // Gets called when the owning unique_ptr is released.
  void operator()(void *ptr) const {
    if (ptr != nullptr)
      free(ptr);
  }
};

// Main type for this memory.
typedef std::unique_ptr<uint8_t, Deleter> Memory;

/// Allocate @param size of memory using malloc; prefault memory if
/// @param prefault is true.
/// Returns a unique_ptr managed memory region with deallocation logic specified
/// in class MallocDeleter.
static Memory allocate(size_t size, bool prefault = false) {
  auto ptr = Memory(reinterpret_cast<uint8_t *>(malloc(size)));
  if (prefault)
    std::memset(ptr.get(), 1, size);
  return ptr;
}

} // namespace m_malloc

} // namespace utils

#endif
