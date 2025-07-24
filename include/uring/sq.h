#ifndef URING_SQ_H
#define URING_SQ_H

#include "uring/barier.h"
#include "uring/params.h"
#include "uring/sqe.h"

namespace liburing {

template <unsigned uring_flags>
class sq {
 public:
  template <unsigned>
  friend class uring;

  sq() noexcept = default;
  ~sq() noexcept = default;

  void setup_ring_pointers(const uring_params<uring_flags> &p) noexcept;

  [[nodiscard]] unsigned ready() const noexcept;
  [[nodiscard]] unsigned space_left() const noexcept;
  [[nodiscard]] sqe *get_sqe() noexcept;
  unsigned flush() noexcept;

  static constexpr unsigned sqe_shift_from_flags(unsigned flags) noexcept;
  static constexpr unsigned sqe_shift() noexcept;
  static constexpr std::size_t sqes_size(unsigned sqes) noexcept;

 private:
  [[nodiscard]] unsigned load_sq_head() const noexcept;

  unsigned *khead_;
  unsigned *ktail_;
  unsigned ring_mask_;
  unsigned ring_entries_;
  unsigned *kflags_;
  unsigned *kdropped_;
  unsigned *array_;
  sqe *sqes_;

  unsigned sqe_head_;
  unsigned sqe_tail_;

  std::size_t ring_sz_;
  void *ring_ptr_;
};

template <unsigned uring_flags>
void sq<uring_flags>::setup_ring_pointers(
    const uring_params<uring_flags> &p) noexcept {
  const auto &off = p.sq_off;

  // clang-format off
  khead_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.head);
  ktail_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.tail);
  ring_mask_ = *reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.ring_mask);
  ring_entries_ = *reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.ring_entries);
  kflags_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.flags);
  kdropped_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.dropped);
  if (!(p.flags & IORING_SETUP_NO_SQARRAY)) { array_ = reinterpret_cast<unsigned *>(reinterpret_cast<uintptr_t>(ring_ptr_) + off.array); }
  // clang-format on
}

template <unsigned uring_flags>
unsigned sq<uring_flags>::ready() const noexcept {
  return sqe_tail_ - load_sq_head();
}

template <unsigned uring_flags>
unsigned sq<uring_flags>::space_left() const noexcept {
  return ring_entries_ - ready();
}

template <unsigned uring_flags>
sqe *sq<uring_flags>::get_sqe() noexcept {
  const unsigned tail = sqe_tail_;
  if (tail - load_sq_head() >= ring_entries_) [[unlikely]] {
    return nullptr;
  }

  sqe *e = &sqes_[(tail & ring_mask_) << sqe_shift()];
  sqe_tail_ = tail + 1;
  return e;
}

template <unsigned uring_flags>
unsigned sq<uring_flags>::flush() noexcept {
  const unsigned tail = sqe_tail_;

  if (sqe_head_ != tail) {
    sqe_head_ = tail;
    if constexpr (!(uring_flags & IORING_SETUP_SQPOLL)) {
      *ktail_ = tail;
    } else {
      io_uring_smp_store_release(ktail_, tail);
    }
  }

  return tail - IO_URING_READ_ONCE(*khead_);
}

template <unsigned uring_flags>
constexpr unsigned sq<uring_flags>::sqe_shift_from_flags(
    const unsigned flags) noexcept {
  return !!(flags & IORING_SETUP_SQE128);
}

template <unsigned uring_flags>
constexpr unsigned sq<uring_flags>::sqe_shift() noexcept {
  return sqe_shift_from_flags(uring_flags);
}

template <unsigned uring_flags>
constexpr std::size_t sq<uring_flags>::sqes_size(unsigned sqes) noexcept {
  sqes <<= sqe_shift_from_flags(uring_flags);
  return sqes * sizeof(sqe);
}

template <unsigned uring_flags>
unsigned sq<uring_flags>::load_sq_head() const noexcept {
  if constexpr (uring_flags & IORING_SETUP_SQPOLL) {
    return io_uring_smp_load_acquire(khead_);
  }
  return *khead_;
}

}  // namespace liburing

#endif  // URING_SQ_H
