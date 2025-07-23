#ifndef URING_CQ_H
#define URING_CQ_H

#include "uring/cqe.h"
#include "uring/params.h"

namespace liburing {

template <unsigned uring_flags>
class cq {
 public:
  template <unsigned>
  friend class uring;

  struct get_data {
    unsigned submit;
    unsigned wait_nr;
    unsigned get_flags;
    int sz;
    int has_ts;
    void *arg;
  };

  cq() noexcept = default;
  ~cq() noexcept = default;

  void setup_ring_pointers(const uring_params<uring_flags> &p) noexcept;

 private:
  static constexpr unsigned cqe_shift_from_flags(unsigned flags) noexcept;
  static constexpr std::size_t cq_size(unsigned cqes) noexcept;

  unsigned *khead_;
  unsigned *ktail_;
  unsigned ring_mask_;
  unsigned ring_entries_;
  unsigned *kflags_;
  unsigned *koverflow_;
  cqe *cqes_;

  std::size_t ring_sz_;
  void *ring_ptr_;
};

template <unsigned uring_flags>
void cq<uring_flags>::setup_ring_pointers(const uring_params<uring_flags> &p) noexcept {
  const auto &off = p.cq_off;

  // clang-format off
  khead_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.head);
  ktail_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.tail);
  ring_mask_ = *reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.ring_mask);
  ring_entries_ = *reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.ring_entries);
  if (off.flags) { kflags_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.flags); }
  koverflow_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.overflow);
  cqes_ = reinterpret_cast<cqe *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.cqes);
  // clang-format on
}

template <unsigned uring_flags>
constexpr unsigned cq<uring_flags>::cqe_shift_from_flags(
    const unsigned flags) noexcept {
  return !!(flags & IORING_SETUP_CQE32);
}

template <unsigned uring_flags>
constexpr std::size_t cq<uring_flags>::cq_size(unsigned cqes) noexcept {
  cqes <<= cqe_shift_from_flags(uring_flags);
  return cqes * sizeof(cqe);
}

}  // namespace liburing

#endif  // URING_CQ_H