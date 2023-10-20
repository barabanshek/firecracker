#include "compression_engine-c.h"
#include "compression_engine.h"

#include "stdio.h"

extern "C" {

void rust_ffi_test(uint32_t data) { 
    acc::rust_ffi_test(data); 
}

int compress(const uint8_t *src, size_t src_size, uint8_t *dst,
             size_t *dst_size, uint8_t job_n) {
  // return acc::compress(src, src_size, dst, dst_size, job_n);
  return 0;
}

int decompress(const uint8_t *src, size_t src_size, uint8_t *dst,
               size_t dst_reserved_size, size_t *dst_actual_size) {
  // return acc::decompress(src, src_size, dst, dst_reserved_size,
  //                        dst_actual_size);
  return 0;
}

}
