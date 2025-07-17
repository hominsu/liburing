#ifndef URING_SQ_H
#define URING_SQ_H

#include "uring/params.h"
#include "uring/sqe.h"

namespace liburing {

class sq {
 public:
  template <unsigned uring_flags>
  friend class uring;

  sq() noexcept = default;
  ~sq() noexcept = default;

  template <unsigned uring_flags>
  void setup_ring_pointers(const uring_params<uring_flags> &p) noexcept {
    const auto &off = p.sq_off;

    khead_ = static_cast<unsigned *>(ring_ptr_ + off.head);
    ktail_ = static_cast<unsigned *>(ring_ptr_ + off.tail);
    ring_mask_ = *static_cast<unsigned *>(ring_ptr_ + off.ring_mask);
    ring_entries_ = *static_cast<unsigned *>(ring_ptr_ + off.ring_entries);
    kflags_ = static_cast<unsigned *>(ring_ptr_ + off.flags);
    kdropped_ = static_cast<unsigned *>(ring_ptr_ + off.dropped);
    if (!(p.flags & IORING_SETUP_NO_SQARRAY)) {
      array_ = static_cast<unsigned *>(ring_ptr_ + off.array);
    }
  }

 private:
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

}  // namespace liburing

#endif  // URING_SQ_H
