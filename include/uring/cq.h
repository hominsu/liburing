#ifndef URING_CQ_H
#define URING_CQ_H

#include "uring/cqe.h"
#include "uring/params.h"

namespace liburing {

class cq {
public:
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

  void setup_ring_pointers(const uring_params &p) noexcept {
    const auto &off = p.cq_off;

    khead_ = static_cast<unsigned *>(ring_ptr_ + off.head);
    ktail_ = static_cast<unsigned *>(ring_ptr_ + off.tail);
    ring_mask_ = *static_cast<unsigned *>(ring_ptr_ + off.ring_mask);
    ring_entries_ = *static_cast<unsigned *>(ring_ptr_ + off.ring_entries);
    if (off.flags) {
      kflags_ = static_cast<unsigned *>(ring_ptr_ + off.flags);
    }
    koverflow_ = static_cast<unsigned *>(ring_ptr_ + off.overflow);
    cqes_ = static_cast<cqe *>(ring_ptr_ + off.cqes);
  }

private:
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

} // namespace liburing

#endif // URING_CQ_H