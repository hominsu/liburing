#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <cstdio>
#include <iostream>
#include <memory>

#include "uring/uring.h"

constexpr std::size_t kQueueDepth = 64;
constexpr std::size_t kBatchSize = 32 * 1024;

enum class event_type : uint8_t {
  NOP,
  READ,
  WRITE,
};

event_type& operator++(event_type& type) {
  switch (type) {
    case event_type::NOP:
      return type = event_type::READ;
    case event_type::READ:
      return type = event_type::WRITE;
    case event_type::WRITE:
      return type = event_type::NOP;
  }
  return type = event_type::NOP;
}

struct io_data {
  explicit io_data(event_type type, std::size_t size, off_t off = 0)
      : iov({.iov_len = size}), type(type), off(off) {
    iov.iov_base = malloc(size);
  }
  ~io_data() {
    free(iov.iov_base);
    iov.iov_len = 0;
    off = 0;
  }

  io_data(const io_data&) = delete;
  io_data& operator=(const io_data&) = delete;

  iovec iov;
  off_t off;
  event_type type;
};

static off_t file_size(const int fd) {
  struct stat st{};
  if (fstat(fd, &st) < 0) {
    throw std::system_error{errno, std::system_category(), "fstat"};
  }

  if (S_ISREG(st.st_mode)) {
    return st.st_size;
  }

  if (S_ISBLK(st.st_mode)) {
    off_t bytes;

    if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
      throw std::system_error{errno, std::system_category(), "ioctl"};
    }

    return bytes;
  }

  return 0;
}

template <unsigned uring_flags>
static void rw_pair(liburing::uring<uring_flags>& ring, std::size_t size,
                    const off_t off, const int in_fd, const int out_fd) {
  auto data = new io_data(event_type::NOP, size, off);
  liburing::sqe* sqe = nullptr;

  sqe = ring.get_sqe();
  if (!sqe) {
    throw std::system_error{-1, std::system_category(), "get_sqe"};
  }
  sqe->prep_readv(in_fd, std::span{&data->iov, 1}, off);
  sqe->flags |= IOSQE_IO_LINK;
  sqe->set_data(data);

  sqe = ring.get_sqe();
  if (!sqe) {
    throw std::system_error{-1, std::system_category(), "get_sqe"};
  }
  sqe->prep_writev(out_fd, std::span{&data->iov, 1}, off);
  sqe->set_data(data);
}

template <unsigned uring_flags>
static void handle_cqe(liburing::uring<uring_flags>& ring, uint64_t& inflight,
                       const liburing::cqe*(&cqe), const int in_fd,
                       const int out_fd) {
  const auto data = reinterpret_cast<io_data*>(cqe->user_data);
  ++data->type;

  if (cqe->res < 0) {
    if (cqe->res == -ECANCELED) {
      rw_pair(ring, data->iov.iov_len, data->off, in_fd, out_fd);
      inflight += 2;
    } else {
      throw std::system_error{-cqe->res, std::system_category(), "cqe error"};
    }
  }

  if (data->type == event_type::WRITE) {
    delete data;
  }

  ring.seen_cqe(cqe);
}

template <unsigned uring_flags>
static void copy_file(liburing::uring<uring_flags>& ring, off_t in_size,
                      const int in_fd, const int out_fd) {
  off_t this_size, off = 0;
  uint64_t inflight = 0;

  while (in_size) {
    uint64_t has_inflight = inflight;
    int depth;

    while (in_size && inflight < kQueueDepth) {
      this_size = kBatchSize;
      if (this_size > in_size) {
        this_size = in_size;
      }
      rw_pair(ring, this_size, off, in_fd, out_fd);
      off += this_size;
      in_size -= this_size;
      inflight += 2;
    }

    if (has_inflight != inflight) {
      ring.submit();
    }

    depth = in_size ? kQueueDepth : 1;

    while (inflight >= depth) {
      const liburing::cqe* cqe;
      int ret = ring.wait_cqe(cqe);
      if (ret < 0) {
        throw std::system_error{-ret, std::system_category(), "wait cqe"};
      }
      handle_cqe(ring, inflight, cqe, in_fd, out_fd);
      --inflight;
    }
  }
}

int main(const int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "%s: infile outfile\n", argv[0]);
    return EXIT_FAILURE;
  }

  int in_fd = open(argv[1], O_RDONLY);
  if (in_fd < 0) {
    throw std::system_error{errno, std::system_category(), "open"};
  }

  int out_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out_fd < 0) {
    throw std::system_error{errno, std::system_category(), "open"};
  }

  liburing::uring<IORING_SETUP_NO_SQARRAY> ring;
  ring.init(kQueueDepth);

  try {
    auto in_size = file_size(in_fd);
    copy_file(ring, in_size, in_fd, out_fd);
  } catch (const std::system_error& e) {
    std::cerr << e.what() << "\n" << e.code() << "\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
  }

  close(in_fd);
  close(out_fd);

  return 0;
}
