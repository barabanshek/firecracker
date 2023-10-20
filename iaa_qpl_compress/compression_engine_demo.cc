#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <tuple>
#include <vector>

#include <chrono>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>

#include "compression_engine.h"

//
static constexpr double kMB = 1024 * 1024;

// GFlags.
DEFINE_string(input_filename, "", "Which file to (de)compress.");

int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // mmap file.
  int fd = open(FLAGS_input_filename.c_str(), O_RDONLY);
  if (fd == -1) {
    LOG(FATAL) << "Error opening memory.file .";
  }

  size_t fd_size = lseek(fd, 0L, SEEK_END);
  lseek(fd, 0L, SEEK_SET);
  LOG(INFO) << "Mapping memory.file of size " << fd_size / kMB << " MB";
  void *src = mmap(nullptr, fd_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (src == nullptr) {
    LOG(FATAL) << "Failed to mmap memory.file .";
  }

  int a = 0;
  for (size_t i = 0; i < fd_size; i += 4096) {
    a += *(reinterpret_cast<uint8_t *>(src) + i);
  }
  std::cout << a << "\n";

  // Define buffers.
  // std::vector<uint8_t> compressed_buff(fd_size / 2, 0);
  acc::CompressedBuffer compressed_buff;
  std::vector<uint8_t> decompressed_buff(fd_size, 0);

  // Compress.
  int chunk_size = 16;
  for (int i = 0; i < chunk_size; ++i) {
    compressed_buff.push_back(
        std::make_tuple(fd_size / chunk_size, 0,
                        std::vector<uint8_t>(fd_size / chunk_size, 1)));
  }

  auto compress_start = std::chrono::high_resolution_clock::now();
  int res = acc::compress(reinterpret_cast<uint8_t *>(src), fd_size,
                          compressed_buff, chunk_size);
  if (res) {
    LOG(FATAL) << "Failed to compress file.";
  }
  auto compress_end = std::chrono::high_resolution_clock::now();
  auto compress_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(compress_end -
                                                            compress_start)
          .count();
  size_t compressed_size = 0;
  for (auto &b : compressed_buff) {
    compressed_size += std::get<1>(b);
  }
  LOG(INFO) << "Compression finished: took= " << compress_duration / 1000
            << " ms, out size= " << compressed_size / kMB << " MB (x"
            << fd_size / compressed_size << ")";

  // Decompress.
  auto decompress_start = std::chrono::high_resolution_clock::now();
  size_t decompressed_size = 0;
  res = acc::decompress(compressed_buff, decompressed_buff.data(),
                        &decompressed_size);
  if (res) {
    LOG(FATAL) << "Failed to decompress file.";
  }
  auto decompress_end = std::chrono::high_resolution_clock::now();
  auto decompress_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(decompress_end -
                                                            decompress_start)
          .count();
  LOG(INFO) << "DeCompression finished: took= " << decompress_duration / 1000
            << " ms";

  // Check.
  if (fd_size != decompressed_size) {
    LOG(FATAL) << "Error: data size missmatch.";
  }

  for (size_t i = 0; i < fd_size; i++) {
    if (((uint8_t *)src)[i] != decompressed_buff[i]) {
      LOG(FATAL) << "Error: data bytes missmatch.";
    }
  }

  LOG(INFO) << "All good!";
  return 0;
}
