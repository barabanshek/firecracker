#ifndef _COMPRESSION_ENGINE_C_H_
#define _COMPRESSION_ENGINE_C_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rust_ffi_test(uint32_t data);

int compress(const uint8_t *src, size_t src_size, uint8_t *dst,
             size_t *dst_size, uint8_t job_n);

int decompress(const uint8_t *src, size_t src_size, uint8_t *dst,
               size_t dst_reserved_size, size_t *dst_actual_size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
