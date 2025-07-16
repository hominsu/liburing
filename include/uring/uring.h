#ifndef URING_URING_H
#define URING_URING_H

#include <system_error>

#include "uring/cq.h"
#include "uring/io_uring.h"
#include "uring/params.h"
#include "uring/sq.h"
#include "uring/syscall.h"

namespace liburing {

template <unsigned uring_flags>
class uring {
 public:
  explicit uring() noexcept = default;
  ~uring() noexcept;

  uring(const uring &) = delete;
  uring(uring &&) = delete;
  uring &operator=(const uring &) = delete;
  uring &operator=(uring &&) = delete;

 private:
  void mmap(int fd, uring_params &p);
  void munmap() noexcept;

  static unsigned sqe_shift_from_flags(unsigned flags) noexcept;
  static std::size_t params_sqes_size(const uring_params &p,
                                      unsigned sqes) noexcept;
  static unsigned cqe_shift_from_flags(unsigned flags) noexcept;
  static std::size_t params_cq_size(const uring_params &p,
                                    unsigned cqes) noexcept;

  sq sq_;
  cq cq_;
  int ring_fd = -1;

  unsigned features;
  int enter_ring_fd;
  uint8_t int_flags;
};

template <unsigned uring_flags>
void uring<uring_flags>::mmap(int fd, uring_params &p) {
  sq_.ring_sz_ = p.sq_off.array + p.sq_entries * sizeof(unsigned);
  cq_.ring_sz_ = p.cq_off.cqes + params_cq_size(p, p.cq_entries);

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

  sq_.sqes_ = __sys_mmap(nullptr, params_sqes_size(p, p.sq_entries),
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
                         IORING_OFF_SQES);
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
unsigned uring<uring_flags>::sqe_shift_from_flags(
    const unsigned flags) noexcept {
  return !!(flags & IORING_SETUP_SQE128);
}

template <unsigned uring_flags>
std::size_t uring<uring_flags>::params_sqes_size(const uring_params &p,
                                                 unsigned sqes) noexcept {
  sqes <<= sqe_shift_from_flags(p.flags);
  return sqes * sizeof(sqe);
}

template <unsigned uring_flags>
unsigned uring<uring_flags>::cqe_shift_from_flags(
    const unsigned flags) noexcept {
  return !!(flags & IORING_SETUP_CQE32);
}

template <unsigned uring_flags>
std::size_t uring<uring_flags>::params_cq_size(const uring_params &p,
                                               unsigned cqes) noexcept {
  cqes <<= sqe_shift_from_flags(p.flags);
  return cqes * sizeof(cqe);
}

}  // namespace liburing

#endif  // URING_URING_H
