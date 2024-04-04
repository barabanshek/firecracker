#ifndef _COMPRESSION_ENGINE_C_H_
#define _COMPRESSION_ENGINE_C_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Just t to check that FFI works.
void rust_ffi_test(uint32_t data);

///
/// Vanila memory restoration API.
///
/// Initialize restoration memory pool with size @param size; this is used to
/// speed-up memory restoration is some cases.
uint8_t InitRestorationMemoryPool(uint64_t size);

/// Clear all partitions currently existing in the current scope's partition
/// list.
void CleanUpPartitions();

/// Add partition with the virtual address @param p_addr of size @param p_size
/// into the current scope's partition list for the further snapshotting with
/// SnapshotPartitions.
void AddPartition(uint64_t p_addr, uint64_t p_size);

/// Snapshot all partitions into the file @param snapshot_filename.
uint8_t SnapshotPartitions(const char *snapshot_filename, uint64_t base_addr);

/// Restore memory according to partitions in @param snapshot_filename into the
/// application memory by @param m_addr of size @param m_size; the applciation
/// must own the memory.
uint8_t RestorePartitions(const char *snapshot_filename, uint64_t m_addr,
                          uint64_t m_size);

///
/// REAP API.
///
/// Initialize REAP recorder over the memory region by @param r_addr of size
/// @param r_size; serve pages from the underlying snapshot file @param
/// snapshot_filename and capture page faults; after recording, dump recorded
/// pages into @param ws_filename; if @param do_compress -- compress ws
/// partitions; after initialization, the REAP recorder starts listening on
/// socket @param sock_filename for the recording commands.
uint8_t InitReapRecorder(const char *sock_filename, uint64_t r_addr,
                         uint64_t r_size, const char *snapshot_filename,
                         const char *ws_filename, uint8_t do_compress = false);

/// Restore pre-recorded pages from @param ws_filename into the memory region by
/// @param r_addr of size @param r_size backed by the underlying snapshot file
/// @param snapshot_filename; if @param do_compress -- restore from compressed
/// ws partitions.
uint8_t RestoreReapSnapshot(uint64_t r_addr, uint64_t r_size,
                            const char *snapshot_filename,
                            const char *ws_filename,
                            uint8_t do_compress = false);

#ifdef __cplusplus
} // extern "C"
#endif

#endif