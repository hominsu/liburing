#ifndef URING_URING_H
#define URING_URING_H

#include <cassert>
#include <cstring>
#include <system_error>

#include "uring/cq.h"
#include "uring/int_flags.h"
#include "uring/io_uring.h"
#include "uring/params.h"
#include "uring/sq.h"
#include "uring/syscall.h"

namespace liburing {

template <unsigned uring_flags = 0>
class uring {
 public:
  explicit uring() noexcept = default;
  ~uring() noexcept;

  uring(const uring &) = delete;
  uring(uring &&) = delete;
  uring &operator=(const uring &) = delete;
  uring &operator=(uring &&) = delete;

  void init(unsigned entries);
  void init(unsigned entries, uring_params<uring_flags> &p);
  void init(unsigned entries, uring_params<uring_flags> &&p);

 private:
  void mmap(int fd, const uring_params<uring_flags> &p);
  void munmap() noexcept;

  static constexpr unsigned sqe_shift_from_flags(unsigned flags) noexcept;
  static constexpr std::size_t sqes_size(unsigned sqes) noexcept;
  static constexpr unsigned cqe_shift_from_flags(unsigned flags) noexcept;
  static constexpr std::size_t cq_size(unsigned cqes) noexcept;

  sq sq_;
  cq cq_;
  int ring_fd_ = -1;

  unsigned features_;
  int enter_ring_fd_;
  uint8_t int_flags_;
};

template <unsigned uring_flags>
uring<uring_flags>::~uring() noexcept {
  if (!(int_flags_ & INT_FLAG_APP_MEM)) {
    __sys_munmap(sq_.sqes_, sqes_size(sq_.ring_entries_));
    munmap();
  }

  if (ring_fd_ != -1) {
    __sys_close(ring_fd_);
  }
}

template <unsigned uring_flags>
void uring<uring_flags>::init(const unsigned entries) {
  init(entries, uring_params<uring_flags>{});
}

template <unsigned uring_flags>
void uring<uring_flags>::init(const unsigned entries,
                              uring_params<uring_flags> &p) {
  assert(ring_fd_ == -1 && "Do not reinit uring");

  const int fd =
      __sys_io_uring_setup(entries, static_cast<io_uring_params *>(&p));
  if (fd < 0) [[unlikely]] {
    throw std::system_error{-fd, std::system_category(),
                            "uring()::__sys_io_uring_setup"};
  }

  ring_fd_ = enter_ring_fd_ = fd;
  features_ = p.features;
  int_flags_ = 0;

  try {
    mmap(fd, p);
  } catch (...) {
    __sys_close(fd);
    std::rethrow_exception(std::current_exception());
  }
}

template <unsigned uring_flags>
void uring<uring_flags>::init(const unsigned entries,
                              uring_params<uring_flags> &&p) {
  init(entries, p);
}

template <unsigned uring_flags>
void uring<uring_flags>::mmap(int fd, const uring_params<uring_flags> &p) {
  sq_.ring_sz_ = p.sq_off.array + p.sq_entries * sizeof(unsigned);
  cq_.ring_sz_ = p.cq_off.cqes + cq_size(p.cq_entries);

  if (p.features & IORING_FEAT_SINGLE_MMAP) {
    sq_.ring_sz_ = cq_.ring_sz_ = std::max(sq_.ring_sz_, cq_.ring_sz_);
  }

  sq_.ring_ptr_ = __sys_mmap(nullptr, sq_.ring_sz_, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
  if (sq_.ring_ptr_ == MAP_FAILED) [[unlikely]] {
    throw std::system_error{errno, std::system_category(),
                            "sq.ring MAP_FAILED"};
  }

  if (p.features & IORING_FEAT_SINGLE_MMAP) {
    cq_.ring_ptr_ = sq_.ring_ptr_;
  } else {
    cq_.ring_ptr_ =
        __sys_mmap(nullptr, cq_.ring_sz_, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    if (cq_.ring_ptr_ == MAP_FAILED) [[unlikely]] {
      cq_.ring_ptr_ = nullptr;
      munmap();
      throw std::system_error{errno, std::system_category(),
                              "cq.ring MAP_FAILED"};
    }
  }

  sq_.sqes_ =
      __sys_mmap(nullptr, sqes_size(p.sq_entries), PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
  if (sq_.sqes_ == MAP_FAILED) [[unlikely]] {
    munmap();
    throw std::system_error{errno, std::system_category(),
                            "sq.sqes MAP_FAILED"};
  }

  sq_.setup_ring_pointers(p);
  cq_.setup_ring_pointers(p);
}

template <unsigned uring_flags>
void uring<uring_flags>::munmap() noexcept {
  if (sq_.ring_sz_) {
    __sys_munmap(sq_.ring_ptr_, sq_.ring_sz_);
  }
  if (cq_.ring_ptr_ && cq_.ring_sz_ && cq_.ring_ptr_ != sq_.ring_ptr_) {
    __sys_munmap(cq_.ring_ptr_, cq_.ring_sz_);
  }
}

template <unsigned uring_flags>
constexpr unsigned uring<uring_flags>::sqe_shift_from_flags(
    const unsigned flags) noexcept {
  return !!(flags & IORING_SETUP_SQE128);
}

template <unsigned uring_flags>
constexpr std::size_t uring<uring_flags>::sqes_size(unsigned sqes) noexcept {
  sqes <<= sqe_shift_from_flags(uring_flags);
  return sqes * sizeof(sqe);
}

template <unsigned uring_flags>
constexpr unsigned uring<uring_flags>::cqe_shift_from_flags(
    const unsigned flags) noexcept {
  return !!(flags & IORING_SETUP_CQE32);
}

template <unsigned uring_flags>
constexpr std::size_t uring<uring_flags>::cq_size(unsigned cqes) noexcept {
  cqes <<= cqe_shift_from_flags(uring_flags);
  return cqes * sizeof(cqe);
}

}  // namespace liburing

#endif  // URING_URING_H
