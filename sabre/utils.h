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

// TODO(Nikita): move somewhere else
namespace sys {

static size_t kPageSize = 4096;
} // namespace sys

namespace utils {
//
/// AUX.
//
#define PACKED_STRUCTURE typedef struct __attribute__((__packed__))

static constexpr uint64_t kkB = 1024;
static constexpr uint64_t kMB = 1024 * 1024;

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

//
/// Memory allocators.
//
class MMapDeleter {
public:
  void operator()(void *ptr) const {
    if (ptr != nullptr)
      munmap(ptr, size_);
    if (fd_ != -1)
      // TODO(Nikita): no needs to clode it here, can be close right after being
      // mmap'ed
      close(fd_);
  }

  void set_size(size_t size) { size_ = size; }
  void set_fd(int fd) { fd_ = fd; }

private:
  size_t size_ = 0;
  int fd_ = -1;
};

class MallocDeleter {
public:
  void operator()(void *ptr) const {
    if (ptr != nullptr)
      free(ptr);
  }
};

static std::unique_ptr<uint8_t, MMapDeleter>
mmap_allocate(size_t size, int fd = -1, bool huge_pages = false,
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

  std::unique_ptr<uint8_t, MMapDeleter> unique_ptr(ptr);
  unique_ptr.get_deleter().set_size(size);
  return unique_ptr;
}

#define mmap_nullptr std::unique_ptr<uint8_t, utils::MMapDeleter>(nullptr)

static std::unique_ptr<uint8_t, MallocDeleter>
malloc_allocate(size_t size, bool prefault = false) {
  auto ptr = std::unique_ptr<uint8_t, MallocDeleter>(
      reinterpret_cast<uint8_t *>(malloc(size)));
  if (prefault)
    std::memset(ptr.get(), 1, size);
  return ptr;
}

#define malloc_nullptr std::unique_ptr<uint8_t, utils::MallocDeleter>(nullptr)

static std::unique_ptr<uint8_t, MMapDeleter> shem_allocate(size_t size,
                                                           int *fd) {
  bool fresh = false;
  if (*fd == -1) {
    const char *name = "shm.file";
    *fd = shm_open(name, O_RDWR | O_CREAT, 0644);
    if (*fd == -1) {
      return std::unique_ptr<uint8_t, MMapDeleter>(nullptr);
    }
    if (ftruncate(*fd, size)) {
      return std::unique_ptr<uint8_t, MMapDeleter>(nullptr);
    }
    fresh = true;
  }
  uint8_t *ptr = reinterpret_cast<uint8_t *>(
      mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0));
  if (ptr == MAP_FAILED)
    return nullptr;

  std::unique_ptr<uint8_t, MMapDeleter> unique_ptr(ptr);
  unique_ptr.get_deleter().set_size(size);
  if (fresh) {
    unique_ptr.get_deleter().set_fd(*fd);
  }
  return unique_ptr;
}

} // namespace utils

#endif
