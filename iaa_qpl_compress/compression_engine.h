#ifndef _COMPRESSION_ENGINE_H_
#define _COMPRESSION_ENGINE_H_

#include <vector>

#include "qpl/qpl.h"

#ifdef _QPL_SOFTWARE_
static constexpr qpl_path_t execution_path = qpl_path_software;
#else
static constexpr qpl_path_t execution_path = qpl_path_hardware;
#endif

namespace acc {

typedef std::vector<std::tuple<size_t, size_t, std::vector<uint8_t>>>
    CompressedBuffer;

void rust_ffi_test(uint32_t data);

int compress(const uint8_t *src, size_t src_size, CompressedBuffer &dst,
             uint8_t job_n = 5);

int decompress(CompressedBuffer &src, uint8_t *dst, size_t *dst_actual_size);

} // namespace acc

#endif
