#ifndef URING_URING_H
#define URING_URING_H

#include "uring/cq.h"
#include "uring/io_uring.h"
#include "uring/params.h"
#include "uring/sq.h"

namespace liburing {

template <unsigned uring_flags> class uring {
public:
  explicit uring() noexcept = default;
  ~uring() noexcept;

  uring(const uring &) = delete;
  uring(uring &&) = delete;
  uring &operator=(const uring &) = delete;
  uring &operator=(uring &&) = delete;

private:
  void mmap(int fd, uring_params &p);

  static std::size_t params_cq_size(const uring_params &p, unsigned cqes);
  static unsigned sqe_shift_from_flags(unsigned flags);
  static unsigned cqe_shift_from_flags(unsigned flags);

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
}

template <unsigned uring_flags>
std::size_t uring<uring_flags>::params_cq_size(const uring_params &p,
                                               unsigned cqes) {
  cqes <<= sqe_shift_from_flags(p.flags);
  return cqes * sizeof(cqe);
}

template <unsigned uring_flags>
unsigned uring<uring_flags>::sqe_shift_from_flags(const unsigned flags) {
  return !!(flags & IORING_SETUP_SQE128);
}

template <unsigned uring_flags>
unsigned uring<uring_flags>::cqe_shift_from_flags(const unsigned flags) {
  return !!(flags & IORING_SETUP_CQE32);
}

} // namespace liburing

#endif // URING_URING_H
