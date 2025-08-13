#ifndef URING_CQ_H
#define URING_CQ_H

#include "uring/barier.h"
#include "uring/cqe.h"
#include "uring/params.h"

namespace liburing {

template <unsigned uring_flags>
class cq {
 public:
  template <unsigned>
  friend class uring;

  struct get_data {
    unsigned submit{};
    unsigned wait_nr{};
    unsigned get_flags{};
    int sz{};
    void *arg = nullptr;
  };

  cq() noexcept = default;
  ~cq() noexcept = default;

  void setup_ring_pointers(const uring_params<uring_flags> &p) noexcept;

  template <typename Fn>
    requires std::invocable<Fn, cqe *>
  unsigned for_each(Fn fn) noexcept(std::is_nothrow_invocable_v<Fn, cqe *>);

  void advance(unsigned nr) noexcept;

  cqe &at(unsigned offset) noexcept;
  const cqe &at(unsigned offset) const noexcept;

  static constexpr unsigned cqe_shift_from_flags(unsigned flags) noexcept;
  static constexpr unsigned cqe_shift() noexcept;
  static constexpr std::size_t cq_size(unsigned cqes) noexcept;

 private:
  unsigned *khead_ = nullptr;
  unsigned *ktail_ = nullptr;
  unsigned ring_mask_{};
  unsigned ring_entries_{};
  unsigned *kflags_ = nullptr;
  unsigned *koverflow_ = nullptr;
  cqe *cqes_ = nullptr;

  std::size_t ring_sz_{};
  void *ring_ptr_ = nullptr;
};

template <unsigned uring_flags>
void cq<uring_flags>::setup_ring_pointers(
    const uring_params<uring_flags> &p) noexcept {
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
template <typename Fn>
  requires std::invocable<Fn, cqe *>
unsigned cq<uring_flags>::for_each(Fn fn) noexcept(
    std::is_nothrow_invocable_v<Fn, cqe *>) {
  unsigned cnt = 0;
  for (auto head = *khead_; head != io_uring_smp_load_acquire(ktail_);
       ++head, ++cnt) {
    cqe *cqe = at(head);
    fn(cqe);
  }
  return cnt;
}

template <unsigned uring_flags>
void cq<uring_flags>::advance(const unsigned nr) noexcept {
  if (nr) [[likely]] {
    io_uring_smp_store_release(khead_, *khead_ + nr);
  }
}

template <unsigned uring_flags>
cqe &cq<uring_flags>::at(const unsigned offset) noexcept {
  return cqes_[(offset & ring_mask_) << cqe_shift()];
}

template <unsigned uring_flags>
const cqe &cq<uring_flags>::at(const unsigned offset) const noexcept {
  return cqes_[(offset & ring_mask_) << cqe_shift()];
}

template <unsigned uring_flags>
constexpr unsigned cq<uring_flags>::cqe_shift_from_flags(
    const unsigned flags) noexcept {
  return !!(flags & IORING_SETUP_CQE32);
}

template <unsigned uring_flags>
constexpr unsigned cq<uring_flags>::cqe_shift() noexcept {
  return cqe_shift_from_flags(uring_flags);
}

template <unsigned uring_flags>
constexpr std::size_t cq<uring_flags>::cq_size(unsigned cqes) noexcept {
  cqes <<= cqe_shift_from_flags(uring_flags);
  return cqes * sizeof(cqe);
}

}  // namespace liburing

#endif  // URING_CQ_H