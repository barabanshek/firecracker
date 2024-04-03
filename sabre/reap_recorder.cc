#include "reap_recorder.h"

#include <algorithm>
#include <cassert>

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "memory_restorator.h"
#include "simple_logging.h"
#include "utils.h"

namespace logging {
static constexpr int _g_log_severity_ = LOG_INFO;
}

namespace acc {

ReapRecorder::ReapRecorder(const std::string &sock_filename)
    : sock_filename_(sock_filename) {}

void ReapRecorder::userfaultfd_handler_record_() {
  uffdio_copy uffdio_copy;

  while (do_record_) {
    uint64_t fault_addr = 0;
    if (UserfaultfdPoll(&fault_addr) == -1) {
      RLOG(0) << "Userfaultfd error, exiting right now...";
      return;
    }

    assert(fault_addr % sys::kPageSize == 0);

    // Install.
    uint64_t offset = fault_addr - reinterpret_cast<uint64_t>(guest_mem_base_);
    uint64_t snapshot_address =
        reinterpret_cast<uint64_t>(snapshot_file_mem_.get()) + offset;

    if (do_write_records_) {
      RLOG(2) << "PF @" << std::hex << (void *)(fault_addr) << std::dec;
      recordings_.push_back(snapshot_address);
    }

    uffdio_copy.src = snapshot_address;
    uffdio_copy.dst = fault_addr;
    uffdio_copy.len = sys::kPageSize;
    uffdio_copy.mode = 0;
    uffdio_copy.copy = 0;
    if (ioctl(uffd_, UFFDIO_COPY, &uffdio_copy) == -1) {
      RLOG(0) << "ioctl-UFFDIO_COPY error.";
      return;
    }
  }
}

void ReapRecorder::sock_handler_() {
  // TODO(Nikita): wtf is 5?
  listen(sock_, 5);
  int msgsock;
  char buff[1024];

  msgsock = accept(sock_, 0, 0);
  if (msgsock == -1) {
    RLOG(0) << "Error accepting incoming connection.";
    return;
  }

  while (do_listen_) {
    bzero(buff, sizeof(buff));
    if (read(msgsock, buff, 1024) < 0) {
      RLOG(0) << "Error reading from socket.";
      return;
    }

    RLOG(1) << "Sock: smth received: " << std::string(buff);
    if (std::string(buff) == "RECORD") {
      RLOG(1) << "Starting recording.";
      recordings_.reserve(1024 * 1024);
      do_write_records_ = true;
    } else if (std::string(buff) == "STOP_RECORD") {
      RLOG(1) << "Stopping recording, total records: " << recordings_.size();
      do_write_records_ = false;
      DumpRecordedPages();
    } else {
      RLOG(0) << "Error: Unknown command received.";
      return;
    }
  }
}

int ReapRecorder::StartListening(uint8_t *mem, size_t size,
                                 const std::string &snapshot_filename,
                                 const std::string &ws_filename) {
  guest_mem_base_ = mem;
  guest_mem_size_ = size;
  ws_filename_ = ws_filename;

  // Create listening socket.
  sock_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock_ < 0) {
    RLOG(0) << "Failed to create listening socket.";
    return -1;
  }
  struct sockaddr_un server;
  server.sun_family = AF_UNIX;
  // TODO(Nikita): avoid hardcodding.
  strcpy(server.sun_path, "/tmp/reap.sock");
  if (bind(sock_, (struct sockaddr *)&server, sizeof(struct sockaddr_un))) {
    RLOG(0) << "Failed to bind listening socket.";
    return -1;
  }
  do_listen_ = true;
  sock_listening_thread_ = std::thread(&ReapRecorder::sock_handler_, this);
  RLOG(1) << "Reap recorder is listening for recording commands.";

  // Mmap underlying snapshot file.
  if (MmapUnderlyingSnapshotFile(snapshot_filename, snapshot_file_mem_)) {
    return -1;
  }

  // Create and enable userfaultfd object.
  if (EnableUserFaultFd(guest_mem_base_, size)) {
    return -1;
  }

  // Create a thread that will process the userfaultfd events.
  do_record_ = true;
  userfaultfd_thread_ =
      std::thread(&ReapRecorder::userfaultfd_handler_record_, this);
  RLOG(1) << "REAP is ready to record pages @" << std::hex
          << (void *)(guest_mem_base_) << ": " << std::dec << size
          << ", PID: " << getpid();

  return 0;
}

int ReapRecorder::StopListening() {
  do_record_ = false;
  do_listen_ = false;
  userfaultfd_thread_.join();
  sock_listening_thread_.join();

  // Unregister userfaultfd handling.
  return DisableUserFaultFd();
}

int ReapRecorder::DumpRecordedPages() {
  // First, join continious partiions.
  std::sort(recordings_.begin(), recordings_.end());

  auto snapshot_mem_base = reinterpret_cast<uint64_t>(snapshot_file_mem_.get());

  MemoryRestorator::MemoryPartitions partitions;
  uint64_t prev_p = 0;
  for (auto const &record : recordings_) {
    assert(snapshot_mem_base <= record &&
           record <= snapshot_mem_base + guest_mem_size_);

    if (partitions.empty()) {
      partitions.push_back(
          std::make_tuple(reinterpret_cast<uint8_t *>(record), sys::kPageSize));
      prev_p = record;
    } else {
      if (prev_p + std::get<1>(partitions.back()) == record) {
        partitions.back() =
            std::make_tuple(std::get<0>(partitions.back()),
                            std::get<1>(partitions.back()) + sys::kPageSize);
      } else {
        partitions.push_back(std::make_tuple(
            reinterpret_cast<uint8_t *>(record), sys::kPageSize));
        prev_p = record;
      }
    }
  }
  RLOG(1) << "Making Reap snapshot: initial #of partition: "
          << recordings_.size() << ", after merging: " << partitions.size();

  // std::cout << "DumpRecordedPages, base: " << (void *)(snapshot_mem_base)
  //           << ", guest_mem_size_= " << guest_mem_size_;
  // for (auto const &[ptr_, size_] : partitions) {
  //   std::cout << "    @" << (void *)(ptr_) << " ("
  //             << ((uint64_t)(ptr_)-snapshot_mem_base) << "), size= " << size_
  //             << ", first_word= " << *(uint64_t *)(ptr_);
  // }

  // Write file.
  acc::MemoryRestorator::MemoryRestoratotConfig cfg = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsScatteredPartitions,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .restored_memory_owner = acc::MemoryRestorator::kUserApplication,
      .max_hardware_jobs = 1,
      .passthrough = false};

  acc::MemoryRestorator memory_restorator(cfg, ws_filename_, nullptr);
  if (memory_restorator.Init()) {
    RLOG(0) << "Failed to dump recordeed pages.";
  }
  return memory_restorator.MakeSnapshot(partitions, snapshot_mem_base);
}

int ReapRecorder::Restore(uint8_t *mem, size_t size,
                          const std::string &snapshot_filename,
                          const std::string &ws_filename) {
  guest_mem_base_ = mem;
  ws_filename_ = ws_filename;

  // Install pages from ws file via MemoryRestorator in passthrough mode.
  // Note: the snapshot in the MemoryRestorator's context here is the ws file.
  acc::MemoryRestorator::MemoryRestoratotConfig cfg = {
      .execution_path = qpl_path_hardware,
      .partition_hanlding_path =
          acc::MemoryRestorator::kHandleAsScatteredPartitions,
      .sigle_partition_handling_path =
          acc::MemoryRestorator::kHandleWithUffdioCopy,
      .restored_memory_owner = acc::MemoryRestorator::kUserApplication,
      .max_hardware_jobs = 1,
      .passthrough = false};

  acc::MemoryRestorator memory_restorator(cfg, ws_filename_, nullptr);
  if (memory_restorator.Init()) {
    RLOG(0) << "Failed to install Reap pages";
    return -1;
  }
  utils::m_mmap::Memory mem_region;
  mem_region.reset(reinterpret_cast<uint8_t *>(guest_mem_base_));
  if (memory_restorator.RestoreFromSnapshot(mem_region, size)) {
    RLOG(0) << "Failed to install Reap pages";
    return -1;
  }

  // Continue execution with underlying snapshot file.
  // Mmap underlying snapshot file.
  if (MmapUnderlyingSnapshotFile(snapshot_filename, snapshot_file_mem_)) {
    return -1;
  }

  // Init userfaultfd thread for pages outside of the ws file.
  if (EnableUserFaultFd(guest_mem_base_, size)) {
    return -1;
  }

  do_record_ = true;
  userfaultfd_thread_ =
      std::thread(&ReapRecorder::userfaultfd_handler_record_, this);
  RLOG(1) << "REAP memory is restored, continuing execution from original "
             "spapshot.";

  return 0;
}

int ReapRecorder::MmapUnderlyingSnapshotFile(
    const std::string &snapshot_filename, utils::m_mmap::Memory &mem) const {
  // Mmap underlying snapshot file.
  int snapshot_fd = open(snapshot_filename.c_str(), O_RDWR);
  if (snapshot_fd == -1) {
    RLOG(0) << "Error during snapshot file open.";
    return -1;
  }
  size_t snapshot_file_size =
      static_cast<size_t>(lseek(snapshot_fd, 0L, SEEK_END));
  lseek(snapshot_fd, 0L, SEEK_SET);

  auto underlying_mem =
      utils::m_mmap::allocate(snapshot_file_size, snapshot_fd);
  if (underlying_mem.get() == nullptr) {
    RLOG(0) << "Error during snapshot file mmap.";
    return -1;
  }
  RLOG(1) << "Snapshot is mmaped, pages are going to be served from "
          << snapshot_filename;

  close(snapshot_fd);
  mem = std::move(underlying_mem);
  return 0;
}

int ReapRecorder::EnableUserFaultFd(const uint8_t *mem, size_t size) {
  uffd_ = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
  if (uffd_ == -1) {
    RLOG(0) << "Failed to create and enable userfaultfd object.";
    return -1;
  }

  uffdio_api_.api = UFFD_API;
  uffdio_api_.features = 0;
  if (ioctl(uffd_, UFFDIO_API, &uffdio_api_) == -1) {
    RLOG(0) << "ioctl-UFFDIO_API.";
    return -1;
  }

  // Register our memory with userfaultfd.
  uffdio_register_.range.start = reinterpret_cast<uint64_t>(mem);
  uffdio_register_.range.len = size;
  uffdio_register_.mode = UFFDIO_REGISTER_MODE_MISSING;
  if (ioctl(uffd_, UFFDIO_REGISTER, &uffdio_register_) == -1) {
    RLOG(0) << "Failed to register memory with userfaultfd: "
            << std::strerror(errno);
    return -1;
  }

  return 0;
}

int ReapRecorder::DisableUserFaultFd() {
  if (ioctl(uffd_, UFFDIO_UNREGISTER, &uffdio_register_) == -1) {
    RLOG(0) << "Failed to unregister memory with userfaultfd.";
    return -1;
  }
  return 0;
}

int ReapRecorder::UserfaultfdPoll(uint64_t *fault_addr) const {
  uffd_msg msg;
  pollfd pollfd;
  pollfd.fd = uffd_;
  pollfd.events = POLLIN;
  int nready = poll(&pollfd, 1, -1);
  if (nready == -1) {
    RLOG(0) << "uffd poll error.";
    return -1;
  }
  ssize_t nread = read(uffd_, &msg, sizeof(msg));
  if (nread == 0) {
    RLOG(0) << "EOF on userfaultfd.";
    return -1;
  }
  if (nread == -1) {
    RLOG(0) << "Failed to read on uffd.";
    return -1;
  }
  if (msg.event != UFFD_EVENT_PAGEFAULT) {
    RLOG(0) << "Unexpected event on userfaultfd.";
    return -1;
  }

  *fault_addr = msg.arg.pagefault.address;
  return 0;
}

} // namespace acc
