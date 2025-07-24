#ifndef URING_URING_H
#define URING_URING_H

#include <sys/mman.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <system_error>

#include "uring/cq.h"
#include "uring/int_flags.h"
#include "uring/io_uring.h"
#include "uring/lib.h"
#include "uring/params.h"
#include "uring/sq.h"
#include "uring/syscall.h"

namespace liburing {

template <unsigned uring_flags = 0>
class uring {
  static constexpr std::size_t kKernelMaxEntries = 32768;
  static constexpr std::size_t kKernelMaxCqEntries = 2 * kKernelMaxEntries;
  static constexpr std::size_t kRingSize = 64;
  static constexpr std::size_t kHugePageSize = 2 * 1024 * 1024;
  static constexpr uint64_t LIBURING_UDATA_TIMEOUT = -1ULL;

  struct _peek_return_type {
    const cqe *_cqe = nullptr;
    unsigned nr_available{};
    int res{};
  };

 public:
  explicit uring() noexcept = default;
  ~uring() noexcept;

  uring(const uring &) = delete;
  uring(uring &&) = delete;
  uring &operator=(const uring &) = delete;
  uring &operator=(uring &&) = delete;

  [[gnu::cold]] void init(unsigned entries, void *buf = nullptr,
                          std::size_t buf_size = 0);
  [[gnu::cold]] void init(unsigned entries, uring_params<uring_flags> &p,
                          void *buf = nullptr, std::size_t buf_size = 0);
  [[gnu::cold]] void init(unsigned entries, uring_params<uring_flags> &&p,
                          void *buf = nullptr, std::size_t buf_size = 0);

  [[nodiscard]] int fd() const noexcept { return ring_fd_; }

  // clang-format off
  int submit() noexcept { return submit_and_wait(0); }
  int submit_and_wait(const unsigned wait_nr) noexcept { return _submit(sq_.flush(), wait_nr, false); }
  // clang-format on

  // clang-format off
  [[nodiscard]] int enter_flags() const noexcept { return int_flags_ & INT_FLAGS_MASK; }
  // clang-format on

  // clang-format off
  [[nodiscard]] unsigned sq_ready() const noexcept { return sq_.ready(); }
  [[nodiscard]] unsigned sq_space_left() const noexcept { return sq_.space_left(); }
  [[nodiscard]] sqe *get_sqe() noexcept { return sq_.get_sqe(); }
  // clang-format on

  int get_cqe(const cqe *(&cqe_ptr), unsigned submit, unsigned wait_nr,
              sigset_t *sigmask) noexcept;
  int wait_cqe_nr(const cqe *(&cqe_ptr), unsigned wait_nr) noexcept;
  int wait_cqe(const cqe *(&cqe_ptr)) noexcept;
  int peek_cqe(const cqe *(&cqe_ptr)) noexcept;
  void seen_cqe(const cqe *cqe) noexcept;

  bool sq_ring_needs_enter(unsigned submit, unsigned &flags) noexcept;
  bool cq_ring_needs_flush() noexcept;
  bool cq_ring_needs_enter() noexcept;

 private:
  void alloc_huge(unsigned entries, uring_params<uring_flags> &p, void *buf,
                  std::size_t buf_size);
  void mmap(int fd, const uring_params<uring_flags> &p);
  void munmap() noexcept;

  static std::pair<unsigned, unsigned> get_sq_cq_entries(
      unsigned entries, const uring_params<uring_flags> &p) noexcept;

  int _submit(unsigned submitted, unsigned wait_nr, bool getevents) noexcept;

  _peek_return_type _peek_cqe() noexcept;

  template <bool has_ts>
  int _get_cqe(const cqe *(&cqe_ptr),
               typename cq<uring_flags>::get_data &data) noexcept;

  sq<uring_flags> sq_;
  cq<uring_flags> cq_;
  int ring_fd_ = -1;

  unsigned features_{};
  int enter_ring_fd_{};
  uint8_t int_flags_{};
};

template <unsigned uring_flags>
uring<uring_flags>::~uring() noexcept {
  if (!(int_flags_ & INT_FLAG_APP_MEM)) {
    __sys_munmap(sq_.sqes_, sq<uring_flags>::sqes_size(sq_.ring_entries_));
    munmap();
  }

  /*
   * Not strictly required, but frees up the slot we used now rather
   * than at process exit time.
   */
  if (int_flags_ & INT_FLAG_REG_RING) {
    // TODO: io_uring_unregister_ring_fd(ring);
  }
  if (ring_fd_ != -1) {
    __sys_close(ring_fd_);
  }
}

template <unsigned uring_flags>
void uring<uring_flags>::init(const unsigned entries, void *buf,
                              const std::size_t buf_size) {
  init(entries, uring_params<uring_flags>{}, buf, buf_size);
}

template <unsigned uring_flags>
void uring<uring_flags>::init(const unsigned entries,
                              uring_params<uring_flags> &p, void *buf,
                              const std::size_t buf_size) {
  assert(ring_fd_ == -1 && "Do not reinit uring");

  if constexpr (uring_flags & IORING_SETUP_REGISTERED_FD_ONLY &&
                !(uring_flags & IORING_SETUP_NO_MMAP)) [[unlikely]] {
    throw std::system_error{
        -EINVAL, std::system_category(),
        "uring()::init, IORING_SETUP_REGISTERED_FD_ONLY only makes sense when "
        "used alongside with IORING_SETUP_NO_MMAP"};
  }

  if constexpr (uring_flags & IORING_SETUP_NO_MMAP) {
    alloc_huge(entries, p, buf, buf_size);
    if (buf) {
      int_flags_ |= INT_FLAG_APP_MEM;
    }
  }

  const int fd =
      __sys_io_uring_setup(entries, static_cast<io_uring_params *>(&p));
  if (fd < 0) [[unlikely]] {
    __sys_munmap(sq_.sqes_, 1);
    munmap();
    throw std::system_error{-fd, std::system_category(),
                            "uring()::__sys_io_uring_setup"};
  }

  if constexpr (!(uring_flags & IORING_SETUP_NO_MMAP)) {
    try {
      mmap(fd, p);
    } catch (...) {
      __sys_close(fd);
      std::rethrow_exception(std::current_exception());
    }
  }

  sq_.setup_ring_pointers(p);
  cq_.setup_ring_pointers(p);

  /*
   * Directly map SQ slots to SQEs
   */
  if constexpr (!(uring_flags & IORING_SETUP_NO_SQARRAY)) {
    for (unsigned *sq_array = sq_.array_, i = 0; i < sq_.ring_entries_; ++i) {
      sq_array[i] = i;
    }
  }
  features_ = p.features;
  ring_fd_ = enter_ring_fd_ = fd;
  if constexpr (uring_flags & IORING_SETUP_REGISTERED_FD_ONLY) {
    ring_fd_ = -1;
    int_flags_ |= INT_FLAG_REG_RING | INT_FLAG_REG_REG_RING;
  } else {
    ring_fd_ = fd;
  }

  /*
   * IOPOLL always needs to enter, except if SQPOLL is set as well.
   * Use an internal flag to check for this.
   */
  if constexpr ((uring_flags & (IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL)) ==
                IORING_SETUP_IOPOLL) {
    int_flags_ |= INT_FLAG_CQ_ENTER;
  }
}

template <unsigned uring_flags>
void uring<uring_flags>::init(const unsigned entries,
                              uring_params<uring_flags> &&p, void *buf,
                              const std::size_t buf_size) {
  init(entries, p, buf, buf_size);
}

template <unsigned uring_flags>
int uring<uring_flags>::get_cqe(const cqe *(&cqe_ptr), unsigned submit,
                                unsigned wait_nr, sigset_t *sigmask) noexcept {
  typename cq<uring_flags>::get_data data{
      .submit = submit,
      .wait_nr = wait_nr,
      .get_flags = 0,
      .sz = _NSIG / 8,
      .arg = sigmask,
  };
  return _get_cqe<false>(cqe_ptr, data);
}

template <unsigned uring_flags>
int uring<uring_flags>::wait_cqe_nr(const cqe *(&cqe_ptr),
                                    const unsigned wait_nr) noexcept {
  return get_cqe(cqe_ptr, 0, wait_nr, nullptr);
}

template <unsigned uring_flags>
int uring<uring_flags>::wait_cqe(const cqe *(&cqe_ptr)) noexcept {
  auto [cqe, nr_available, res] = _peek_cqe();
  if (!res && cqe) {
    cqe_ptr = cqe;
    return 0;
  }
  return wait_cqe_nr(cqe_ptr, 1);
}

template <unsigned uring_flags>
int uring<uring_flags>::peek_cqe(const cqe *(&cqe_ptr)) noexcept {
  auto [cqe, nr_available, res] = _peek_cqe();
  if (!res && cqe) {
    cqe_ptr = cqe;
    return 0;
  }
  return wait_cqe_nr(cqe_ptr, 0);
}

template <unsigned uring_flags>
void uring<uring_flags>::seen_cqe([[maybe_unused]] const cqe *cqe) noexcept {
  assert(cqe);
  cq_.advance(1);
}

template <unsigned uring_flags>
bool uring<uring_flags>::sq_ring_needs_enter(const unsigned submit,
                                             unsigned &flags) noexcept {
  if (!submit) {
    return false;
  }

  if constexpr (!(uring_flags & IORING_SETUP_SQPOLL)) {
    return true;
  }

  /*
   * Ensure the kernel can see the store to the SQ tail before we read
   * the flags.
   */
  io_uring_smp_mb();

  if (IO_URING_READ_ONCE(*sq_.kflags_) & IORING_SQ_NEED_WAKEUP) [[unlikely]] {
    flags |= IORING_ENTER_SQ_WAKEUP;
    return true;
  }

  return false;
}

template <unsigned uring_flags>
bool uring<uring_flags>::cq_ring_needs_flush() noexcept {
  return IO_URING_READ_ONCE(*sq_.kflags_) &
         (IORING_SQ_CQ_OVERFLOW | IORING_SQ_TASKRUN);
}

template <unsigned uring_flags>
bool uring<uring_flags>::cq_ring_needs_enter() noexcept {
  return int_flags_ & INT_FLAG_CQ_ENTER || cq_ring_needs_flush();
}

template <unsigned uring_flags>
void uring<uring_flags>::alloc_huge(const unsigned entries,
                                    uring_params<uring_flags> &p, void *buf,
                                    std::size_t buf_size) {
  const auto [sq_entries, cq_entries] = get_sq_cq_entries(entries, p);
  if (!sq_entries || !cq_entries) [[unlikely]] {
    throw std::system_error{-EINVAL, std::system_category(),
                            "uring::get_sq_cq_entries"};
  }

  const std::size_t page_size = get_page_size();

  std::size_t sqes_mem = sq<uring_flags>::sqes_size(sq_entries);
  if constexpr (!(uring_flags & IORING_SETUP_NO_SQARRAY)) {
    sqes_mem += sq_entries * sizeof(unsigned);
  }
  sqes_mem = (sqes_mem + page_size - 1) & ~(page_size - 1);

  std::size_t ring_mem = kRingSize;
  ring_mem += sqes_mem + cq<uring_flags>::cq_size(cq_entries);

  const std::size_t mem_used = (ring_mem + page_size - 1) & ~(page_size - 1);

  void *ptr;
  if (buf) {
    if (mem_used > buf_size) [[unlikely]] {
      throw std::system_error{-ENOMEM, std::system_category(),
                              "uring()::alloc_huge"};
    }

    ptr = buf;
  } else {
    if (sqes_mem > kHugePageSize || ring_mem > kHugePageSize) [[unlikely]] {
      throw std::system_error{-ENOMEM, std::system_category(),
                              "uring()::alloc_huge"};
    }

    int map_hugetlb = sqes_mem > page_size ? MAP_HUGETLB : 0;
    buf_size = map_hugetlb ? kHugePageSize : page_size;

    ptr = __sys_mmap(nullptr, buf_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS | map_hugetlb, -1, 0);
    if (ptr == MAP_FAILED) [[unlikely]] {
      throw std::system_error{errno, std::system_category(), "ptr MAP_FAILED"};
    }
  }

  sq_.sqes_ = static_cast<sqe *>(ptr);
  if (mem_used <= buf_size) {
    sq_.ring_ptr_ = sq_.sqes_ + sqes_mem;
    sq_.ring_sz_ = 0;
    cq_.ring_sz_ = 0;
  } else {
    int map_hugetlb = sqes_mem > page_size ? MAP_HUGETLB : 0;
    buf_size = map_hugetlb ? kHugePageSize : page_size;

    ptr = __sys_mmap(nullptr, buf_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS | map_hugetlb, -1, 0);
    if (ptr == MAP_FAILED) [[unlikely]] {
      __sys_munmap(sq_.sqes_, 1);
      throw std::system_error{errno, std::system_category(), "ptr MAP_FAILED"};
    }

    sq_.ring_ptr_ = ptr;
    sq_.ring_sz_ = buf_size;
    cq_.ring_sz_ = 0;
  }

  cq_.ring_ptr_ = sq_.ring_ptr_;
  p.sq_off.user_addr = reinterpret_cast<uint64_t>(sq_.sqes_);
  p.cq_off.user_addr = reinterpret_cast<uint64_t>(sq_.ring_ptr_);
}

template <unsigned uring_flags>
void uring<uring_flags>::mmap(int fd, const uring_params<uring_flags> &p) {
  sq_.ring_sz_ = p.sq_off.array + p.sq_entries * sizeof(unsigned);
  cq_.ring_sz_ = p.cq_off.cqes + cq<uring_flags>::cq_size(p.cq_entries);

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

  sq_.sqes_ = static_cast<sqe *>(__sys_mmap(
      nullptr, sq<uring_flags>::sqes_size(p.sq_entries), PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES));
  if (sq_.sqes_ == MAP_FAILED) [[unlikely]] {
    munmap();
    throw std::system_error{errno, std::system_category(),
                            "sq.sqes MAP_FAILED"};
  }
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
std::pair<unsigned, unsigned> uring<uring_flags>::get_sq_cq_entries(
    unsigned entries, const uring_params<uring_flags> &p) noexcept {
  unsigned cq_entries;

  if (!entries || entries > kKernelMaxEntries) {
    return {0, 0};
  }
  entries = std::bit_ceil(entries);

  if constexpr (uring_flags & IORING_SETUP_CQSIZE) {
    if (!p.cq_entries || p.cq_entries > kKernelMaxCqEntries ||
        p.cq_entries < entries) {
      return {0, 0};
    }
    cq_entries = std::bit_ceil(p.cq_entries);
  } else {
    cq_entries = 2 * entries;
  }

  return {entries, cq_entries};
}

template <unsigned uring_flags>
int uring<uring_flags>::_submit(const unsigned submitted, unsigned wait_nr,
                                bool getevents) noexcept {
  const bool cq_needs_enter = getevents || wait_nr || cq_ring_needs_enter();
  unsigned flags = enter_flags();

  if (sq_ring_needs_enter(submitted, flags) || cq_needs_enter) {
    if (cq_needs_enter) {
      flags |= IORING_ENTER_GETEVENTS;
    }
    return __sys_io_uring_enter(enter_ring_fd_, submitted, wait_nr, flags,
                                nullptr);
  }

  return static_cast<int>(submitted);
}

template <unsigned uring_flags>
typename uring<uring_flags>::_peek_return_type
uring<uring_flags>::_peek_cqe() noexcept {
  _peek_return_type ret;

  while (true) {
    const unsigned tail = io_uring_smp_load_acquire(cq_.ktail_);
    const unsigned head = *cq_.khead_;

    ret._cqe = nullptr;
    ret.nr_available = tail - head;
    if (!ret.nr_available) {
      break;
    }

    ret._cqe =
        &cq_.cqes_[(head & cq_.ring_mask_) << cq<uring_flags>::cqe_shift()];
    if (!(features_ & IORING_FEAT_EXT_ARG) &&
        ret._cqe->user_data == LIBURING_UDATA_TIMEOUT) [[unlikely]] {
      ret.res = ret._cqe->res < 0 ? ret._cqe->res : 0;
      cq_.advance(1);
      if (!ret.res) {
        continue;
      }
      ret._cqe = nullptr;
    }

    break;
  }

  return ret;
}

template <unsigned uring_flags>
template <bool has_ts>
int uring<uring_flags>::_get_cqe(
    const cqe *(&cqe_ptr), typename cq<uring_flags>::get_data &data) noexcept {
  _peek_return_type peek_ret;
  bool looped = false;
  int err = 0;

  while (true) {
    bool need_enter = false;
    unsigned flags = enter_flags();

    peek_ret = _peek_cqe();
    auto [cqe, nr_available, res] = peek_ret;
    if (res) [[unlikely]] {
      err = !err ? res : err;
      break;
    }

    if (!cqe && !data.wait_nr && !data.submit) {
      /*
       * If we already looped once, we already entered
       * the kernel. Since there's nothing to submit or
       * wait for, don't keep retrying.
       */
      if (looped || !cq_ring_needs_enter()) {
        err = !err ? -EAGAIN : err;
        break;
      }
      need_enter = true;
    }

    if (data.wait_nr > nr_available || need_enter) {
      flags |= IORING_ENTER_GETEVENTS | data.get_flags;
      need_enter = true;
    }
    if (sq_ring_needs_enter(data.submit, flags)) need_enter = true;
    if (!need_enter) {
      break;
    }
    if constexpr (has_ts) {
      if (looped) {
        io_uring_getevents_arg *arg = data.arg;
        if (!cqe && arg->ts && !err) {
          err = -ETIME;
        }
        break;
      }
    }

    int ret = __sys_io_uring_enter2(enter_ring_fd_, data.submit, data.wait_nr,
                                    flags, data.arg, data.sz);
    if (ret < 0) [[unlikely]] {
      err = !err ? ret : err;
      break;
    }
    data.submit -= ret;
    if (cqe) {
      break;
    }
    if (!looped) {
      looped = true;
      err = ret;
    }
  }

  cqe_ptr = peek_ret._cqe;
  return err;
}

}  // namespace liburing

#endif  // URING_URING_H
