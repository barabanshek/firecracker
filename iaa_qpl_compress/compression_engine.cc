#include <cassert>
#include <iostream>
#include <memory>
#include <numeric>

#include <chrono>

#include <unistd.h>

#include "compression_engine.h"

namespace acc {

void rust_ffi_test(uint32_t data) {
  std::cout << "Hi from C++, I just got " << data << std::endl;
}

int compress(const uint8_t *src, size_t src_size, CompressedBuffer &dst,
             uint8_t job_n) {
  assert(dst.size() == job_n);

  // Job initialization.
  uint32_t job_size = 0;
  qpl_status status = qpl_get_job_size(execution_path, &job_size);
  if (status != QPL_STS_OK) {
    // LOG(WARNING) << "An error acquired during job size getting.";
    return -1;
  }

  // Prepare parallel jobs.
  std::vector<std::vector<uint8_t>> job_buffer;
  for (int i = 0; i < job_n; ++i) {
    job_buffer.push_back(std::vector<uint8_t>(job_size));
  }

  // Parallel-compress.
  size_t job_iteration = 0;
  size_t batch_size = src_size / job_n;
  assert(std::get<0>(dst.front()) >= batch_size);
  assert(std::get<2>(dst.front()).size() >= batch_size);
  for (auto &job_raw : job_buffer) {
    qpl_job *job = reinterpret_cast<qpl_job *>(job_raw.data());
    status = qpl_init_job(execution_path, job);
    if (status != QPL_STS_OK) {
      // LOG(WARNING) << "An error acquired during compression job
      // initializing.";
      return -1;
    }

    // Compress.
    job->op = qpl_op_compress;
    job->level = qpl_default_level;
    job->next_in_ptr = const_cast<uint8_t *>(src + job_iteration * batch_size);
    job->next_out_ptr = std::get<2>(dst[job_iteration]).data();
    if (src_size - job_iteration * batch_size < batch_size)
      job->available_in = src_size - job_iteration * batch_size;
    else
      job->available_in = batch_size;
    job->available_out = std::get<0>(dst[job_iteration]);
    job->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_DYNAMIC_HUFFMAN |
                 QPL_FLAG_OMIT_VERIFY;
    status = qpl_submit_job(job);
    if (status != QPL_STS_OK) {
      // LOG(WARNING) << "An error acquired during job finalization.";
      return -1;
    }

    ++job_iteration;
  }

  // Wait until compressed and gather.
  std::vector<uint8_t> cmpl(job_n, 0);
  while (std::reduce(cmpl.begin(), cmpl.end()) != job_n) {
    for (int idx = 0; idx < job_buffer.size(); ++idx) {
      if (cmpl[idx] == 0) {
        qpl_job *job = reinterpret_cast<qpl_job *>(job_buffer[idx].data());
        if (qpl_check_job(job) == QPL_STS_OK) {
          std::get<1>(dst[idx]) = job->total_out;
          cmpl[idx] = 1;
        }
      }
    }
  }

  return 0;
}

int decompress(CompressedBuffer &src, uint8_t *dst, size_t *dst_actual_size) {
  // Job initialization.
  uint32_t job_size = 0;
  qpl_status status = qpl_get_job_size(execution_path, &job_size);
  if (status != QPL_STS_OK) {
    // LOG(WARNING) << "An error acquired during job size getting.";
    return -1;
  }

  // Prepare parallel jobs.
  std::vector<std::vector<uint8_t>> job_buffer;
  for (int i = 0; i < src.size(); ++i) {
    job_buffer.push_back(std::vector<uint8_t>(job_size));
  }

  // Parallel-decompress.
  size_t byte_cnt = 0;
  size_t job_iteration = 0;
  for (auto &job_raw : job_buffer) {
    qpl_job *job = reinterpret_cast<qpl_job *>(job_raw.data());
    status = qpl_init_job(execution_path, job);
    if (status != QPL_STS_OK) {
      // LOG(WARNING) << "An error acquired during compression job
      // initializing.";
      return -1;
    }

    // Compress.
    job->op = qpl_op_decompress;
    job->level = qpl_default_level;
    job->next_in_ptr = std::get<2>(src[job_iteration]).data();
    job->next_out_ptr = dst + byte_cnt;
    job->available_in = std::get<1>(src[job_iteration]);
    job->available_out = std::get<0>(src[job_iteration]);
    job->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST;
    status = qpl_submit_job(job);
    if (status != QPL_STS_OK) {
      // LOG(WARNING) << "An error acquired during job finalization.";
      return -1;
    }

    byte_cnt += std::get<0>(src[job_iteration]);
    ++job_iteration;
  }

  // Wait until compressed and gather.
  size_t decompressed_size = 0;
  std::vector<uint8_t> cmpl(src.size(), 0);
  while (std::reduce(cmpl.begin(), cmpl.end()) != src.size()) {
    for (int idx = 0; idx < job_buffer.size(); ++idx) {
      if (cmpl[idx] == 0) {
        qpl_job *job = reinterpret_cast<qpl_job *>(job_buffer[idx].data());
        if (qpl_check_job(job) == QPL_STS_OK) {
          decompressed_size += job->total_out;
          cmpl[idx] = 1;
        }
      }
    }
  }

  *dst_actual_size = decompressed_size;
  return 0;
}

} // namespace acc
