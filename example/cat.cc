#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <cstdio>
#include <iostream>
#include <memory>

#include "uring/uring.h"

constexpr std::size_t kQueueDepth = 2;
constexpr std::size_t kBatchSize = 64 * 1024;

struct io_data {
  explicit io_data(const std::size_t size, const off_t off = 0)
      : iov({.iov_len = size}), off(off) {
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
};

template <unsigned uring_flags>
static void cat_splice(liburing::uring<uring_flags>& ring, const int in_fd,
                       const struct stat& in_st, const int out_fd,
                       const struct stat& out_st) {
  (void)out_st;

  off_t remaining = in_st.st_size, off = 0;
  int pipe_size = fcntl(out_fd, F_GETPIPE_SZ);
  if (pipe_size == -1) {
    throw std::system_error{errno, std::system_category(), "fcntl"};
  }

  while (remaining > 0) {
    off_t to_read = std::min(remaining, static_cast<off_t>(pipe_size));

    {
      liburing::sqe* sqe = ring.get_sqe();
      if (!sqe) {
        throw std::system_error{-1, std::system_category(), "get_sqe"};
      }
      sqe->prep_splice(in_fd, off, out_fd, -1, to_read, 0);

      ring.submit();
    }

    {
      const liburing::cqe* cqe;
      int ret = ring.wait_cqe(cqe);
      if (ret < 0) {
        throw std::system_error{-ret, std::system_category(), "wait cqe"};
      }

      ring.seen_cqe(cqe);
      if (cqe->res < 0) {
        throw std::system_error{-cqe->res, std::system_category(), "cqe error"};
      }

      remaining -= cqe->res;
      off += cqe->res;
    }
  }
}

template <unsigned uring_flags>
static void cat_fallback(liburing::uring<uring_flags>& ring, const int in_fd,
                         const struct stat& in_st, const int out_fd,
                         const struct stat& out_st) {
  (void)out_st;
  io_data data(kBatchSize);
  off_t remaining = in_st.st_size, off = 0;

  uint64_t inflight = 0;
  while (remaining > 0) {
    off_t to_read = std::min(remaining, static_cast<off_t>(kBatchSize));
    data.iov.iov_len = to_read;

    {
      liburing::sqe* sqe = ring.get_sqe();
      if (!sqe) {
        throw std::system_error{-1, std::system_category(), "get_sqe"};
      }
      sqe->prep_readv(in_fd, std::span{&data.iov, 1}, off);
      sqe->flags |= IOSQE_IO_LINK;
      sqe->set_data(&data);

      sqe = ring.get_sqe();
      if (!sqe) {
        throw std::system_error{-1, std::system_category(), "get_sqe"};
      }
      sqe->prep_writev(out_fd, std::span{&data.iov, 1}, -1);
      sqe->set_data(&data);

      ring.submit();
      inflight += 2;
    }

    while (inflight > 0) {
      const liburing::cqe* cqe;
      int ret = ring.wait_cqe(cqe);
      if (ret < 0) {
        throw std::system_error{-ret, std::system_category(), "wait cqe"};
      }

      ring.seen_cqe(cqe);
      if (cqe->res < 0) {
        throw std::system_error{-cqe->res, std::system_category(), "cqe error"};
      }

      --inflight;
    }

    off += to_read;
    remaining -= to_read;
  }
}

template <unsigned uring_flags>
static void cat(liburing::uring<uring_flags>& ring, const int in_fd,
                const int out_fd) {
  struct stat in_st{};
  if (fstat(in_fd, &in_st) < 0) {
    throw std::system_error{errno, std::system_category(), "fstat"};
  }

  struct stat out_st{};
  if (fstat(out_fd, &out_st) < 0) {
    throw std::system_error{errno, std::system_category(), "fstat"};
  }

  if (S_ISREG(in_st.st_mode) && S_ISFIFO(out_st.st_mode)) {  // splice
    cat_splice(ring, in_fd, in_st, out_fd, out_st);
  } else {  // fallback
    cat_fallback(ring, in_fd, in_st, out_fd, out_st);
  }
}

/**
 * dd if=/dev/urandom of=1gbfile bs=1G count=1
 *
 * splice:
 *      sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
 *      time ./cat 1gbfile | pv -r > /dev/null
 *
 * fallback:
 *      sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
 *      time ./cat 1gbfile > 1gbfile.out
 */
int main(const int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "%s: file\n", argv[0]);
    return EXIT_FAILURE;
  }

  const int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    throw std::system_error{errno, std::system_category(), "open"};
  }

  liburing::uring<IORING_SETUP_NO_SQARRAY> ring;
  ring.init(kQueueDepth);

  try {
    cat(ring, fd, STDOUT_FILENO);
  } catch (const std::system_error& e) {
    std::cerr << e.what() << "\n" << e.code() << "\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
  }

  close(fd);

  return 0;
}