#include "memory_restorator-c.h"
#include "mem_pool.h"
#include "memory_restorator.h"
#include "reap_recorder.h"

#include "simple_logging.h"

extern "C" {

namespace logging {
static constexpr int _g_log_severity_ = LOG_INFO;
}

void rust_ffi_test(uint32_t data) { RLOG(1) << "Hi from Rust: " << data; }

// Static state objects; these objects are used to represent certain state
// required for memroy restoration inside the Rust environment of the
// firecracker codebase (sine we can not use classes/objects with FFI); the life
// time of these objects is defined by the applcaition scope same as any other
// static object; CAVEAT: in firecracker, this might be the scope of the VMM or
// the actual VMs, individually.
namespace {

// Static object to store partition info received from Firecracker; this object
// must span all calls to AddPartition/CleanUpPartitions and consequent
// SnapshotPartitions.
// Expected firecracker scope: individual VMs.
static acc::MemoryRestorator::MemoryPartitions _sPartitions;

// Static object to store the memory pool used for the intermediate
// restoration/decompression buffer inside the memory restorator.
// Expected firecracker scope: VMM.
static MemoryPool _sRestoratorMemoryPool;

// REAP recorder.
acc::ReapRecorder _sReapRecorder;
} // namespace

// Initialize _sRestoratorMemoryPool.
// Static state object: _sRestoratorMemoryPool.
// Dependent FFI functions: None.
uint8_t InitRestorationMemoryPool(uint64_t size) {
  return _sRestoratorMemoryPool.Init(size) == 0 ? 0 : 1;
}

// Just a helper to empty all partition info currently existing in _sPartitions.
// Static state object: _sPartitions.
// Dependent FFI functions: AddPartition, SnapshotPartitions.
void CleanUpPartitions() { _sPartitions.clear(); }

// Append a partition into _sPartitions; the information will live as long as
// the current application scope exists.
// Static state object: _sPartitions.
// Dependent FFI functions: SnapshotPartitions, CleanUpPartitions.
void AddPartition(uint64_t p_addr, uint64_t p_size) {
  _sPartitions.push_back(
      std::make_tuple(reinterpret_cast<uint8_t *>(p_addr), p_size));
  RLOG(1) << "Partition added: " << std::hex << p_addr << ", " << std::dec
          << p_size;
}

// Snapshot partitions currently exsiting in _sPartitions into a file.
// Static state object: _sPartitions.
// Dependent FFI functions: AddPartition, CleanUpPartitions.
uint8_t SnapshotPartitions(const char *snapshot_filename, uint64_t base_addr) {
  // TODO(Nikita): think about moving this out to the global hypervisor scope.
  acc::MemoryRestorator memory_restorator(acc::MemoryRestorator::default_cfg,
                                          snapshot_filename);
  if (memory_restorator.Init()) {
    RLOG(0) << "Failed to init QPL.";
    return 1;
  }

  if (memory_restorator.MakeSnapshot(_sPartitions, base_addr)) {
    RLOG(0) << "Failed to make snapshot.";
    return 1;
  }

  if (memory_restorator.DropCaches()) {
    RLOG(0) << "Failed to drop caches.";
    return 1;
  }

  RLOG(0) << "Snapshot created, and its page cache is dropped.";
  return 0;
}

/// Restore memory according to partitions in @param snapshot_filename into the
/// application memory by @param m_addr of size @param m_size.
// Static state object: None.
// Dependent FFI functions: None.
uint8_t RestorePartitions(const char *snapshot_filename, uint64_t m_addr,
                          uint64_t m_size) {
  // TODO(Nikita): think about moving this out to the global hypervisor scope.
  acc::MemoryRestorator memory_restorator(
      acc::MemoryRestorator::default_cfg, snapshot_filename,
      _sRestoratorMemoryPool.IsInitialized() ? &_sRestoratorMemoryPool
                                             : nullptr);
  if (memory_restorator.Init()) {
    RLOG(0) << "Failed to init QPL.";
    return 1;
  }

  utils::m_mmap::Memory mem_region;
  mem_region.reset(reinterpret_cast<uint8_t *>(m_addr));
  if (memory_restorator.RestoreFromSnapshot(mem_region, m_size)) {
    RLOG(0) << "Failed to restore memory.";
    return 1;
  }

  mem_region.release();
  return 0;
}

// Static state object: _sReapRecorder.
// Dependent FFI functions: None.
uint8_t InitReapRecorder(const char *sock_filename, uint64_t r_addr,
                         uint64_t r_size, const char *snapshot_filename,
                         const char *ws_filename, uint8_t do_compress) {
  _sReapRecorder.Init(sock_filename, static_cast<bool>(do_compress));
  if (_sReapRecorder.StartListening(reinterpret_cast<uint8_t *>(r_addr), r_size,
                                    snapshot_filename, ws_filename)) {
    RLOG(0) << "Failed to init reap recorder.";
    return 1;
  }
  RLOG(1) << "Reap recorder is initialized: underlying snapshot file: "
          << snapshot_filename << ", output ws file: " << ws_filename;
  return 0;
}

// Static state object: _sReapRecorder.
// Dependent FFI functions: None.
uint8_t RestoreReapSnapshot(uint64_t r_addr, uint64_t r_size,
                            const char *snapshot_filename,
                            const char *ws_filename, uint8_t do_compress) {
  _sReapRecorder.Init(static_cast<bool>(do_compress));
  if (_sReapRecorder.Restore(reinterpret_cast<uint8_t *>(r_addr), r_size,
                             snapshot_filename, ws_filename)) {
    RLOG(0) << "Failed to restore reap snapshot.";
    return 1;
  }
  return 0;
}
}
