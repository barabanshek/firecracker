#ifndef _REAP_RECORDER_H_
#define _REAP_RECORDER_H_

#include <linux/userfaultfd.h>
#include <poll.h>

#include <thread>
#include <vector>

#include "utils.h"

namespace acc {

class ReapRecorder {
public:
  ReapRecorder();

  void Init(const std::string &sock_filename, bool do_compress = false);
  void Init(bool do_compress = false);

  /// Init page recording in the region @param mem of size @param size.
  /// After this call, @param mem becomes being served via user space page fault
  /// handling from @param snapshot_filename, and the ReapRecorder starts
  /// listening on sock_filename to start collection of the page access
  /// statistics.
  int StartListening(uint8_t *mem, size_t size,
                     const std::string &snapshot_filename,
                     const std::string &ws_filename);

  /// Restore from snapshot base snapshot @param snapshot_filename using ws file
  /// @param ws_filename into the memory region at @param mem of size @param
  /// size.
  int Restore(uint8_t *mem, size_t size, const std::string &snapshot_filename,
              const std::string &ws_filename);

  /// Stop listening for both page faults and sock_filename and unregister
  /// userfaultfd.
  int StopListening();

private:
  std::string sock_filename_;
  utils::m_mmap::Memory snapshot_file_mem_;
  utils::m_malloc::Memory ws_file_mem_;

  std::string ws_filename_;
  bool do_compress_;

  int sock_ = -1;
  std::thread sock_listening_thread_;
  volatile bool do_listen_ = false;
  void sock_handler_();

  uffdio_api uffdio_api_;
  uffdio_register uffdio_register_;
  long uffd_;
  uint8_t *guest_mem_base_;
  size_t guest_mem_size_;

  std::thread userfaultfd_thread_;
  volatile bool do_record_ = false;
  volatile bool do_serve_ = false;
  int UserfaultfdPoll(uint64_t *fault_addr) const;
  void userfaultfd_handler_record_();

  volatile bool do_write_records_ = false;
  std::vector<uint64_t> recordings_;

  int MmapUnderlyingSnapshotFile(const std::string &snapshot_filename,
                                 utils::m_mmap::Memory &mem) const;

  int EnableUserFaultFd(const uint8_t *mem, size_t size);
  int DisableUserFaultFd();

  /// Dump all recorder so far dirty pages to a file @param snapshot_filename.
  int DumpRecordedPages();
};

} // namespace acc

#endif
