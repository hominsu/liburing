#include <sys/uio.h>

#include <cstdio>
#include <filesystem>
#include <iostream>

#include "uring/cqe.h"
#include "uring/sqe.h"
#include "uring/uring.h"

constexpr std::size_t kEntries = 8;
constexpr std::size_t kBlockSize = 1 << 12;

struct io_data {
  std::size_t size;
  iovec iovecs[];
};

io_data* submit(liburing::uring<>& ring, int fd, std::size_t size) {
  const std::size_t blocks = size + kBlockSize - 1 & ~(kBlockSize - 1);
  auto* data =
      static_cast<io_data*>(malloc(sizeof(io_data) + blocks * sizeof(iovec)));
  data->size = size;

  for (std::size_t s = size, i = 0; s != 0; ++i) {
    const std::size_t to_read = std::min(s, kBlockSize);
    data->iovecs[i].iov_len = to_read;
    if (posix_memalign(&data->iovecs[i].iov_base, kBlockSize, kBlockSize)) {
      throw std::system_error{errno, std::system_category(), "posix_memalign"};
    }
    s -= to_read;
  }

  liburing::sqe* sqe = ring.get_sqe();
  sqe->prep_readv(fd, std::span{data->iovecs, blocks}, 0);
  sqe->set_data(data);

  ring.submit();
  return data;
}

void print_result(liburing::uring<>& ring) {
  const liburing::cqe* cqe;
  ring.wait_cqe(cqe);

  auto* data = reinterpret_cast<io_data*>(cqe->user_data);
  const std::size_t blocks = data->size + kBlockSize - 1 & ~(kBlockSize - 1);
  std::cout << writev(STDOUT_FILENO, data->iovecs, static_cast<int>(blocks)) << std::endl;

  ring.seen_cqe(cqe);
}

int main(const int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "%s: file\n", argv[0]);
    return 1;
  }

  liburing::uring ring;
  ring.init(kEntries);

  auto path = std::filesystem::path(argv[1]);
  int fd = open(path.c_str(), O_RDONLY | O_DIRECT);
  if (fd < 0) {
    throw std::system_error{errno, std::system_category(), "open"};
  }

  io_data* data = nullptr;

  try {
    data = submit(ring, fd, std::filesystem::file_size(path));
    print_result(ring);
  } catch (const std::system_error& e) {
    std::cerr << e.what() << "\n" << e.code() << "\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
  }

  free(data);
  close(fd);

  return 0;
}
