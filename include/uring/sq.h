#ifndef URING_SQ_H
#define URING_SQ_H

#include "uring/sqe.h"

namespace liburing {

struct sq {
  sq() noexcept = default;
  ~sq() noexcept = default;

private:
  unsigned *khead;
  unsigned *ktail;
  unsigned ring_mask;
  unsigned ring_entries;
  unsigned *kflags;
  unsigned *kdropped;
  unsigned *array;
  sqe *sqes;

  unsigned sqe_head;
  unsigned sqe_tail;

  std::size_t ring_sz;
  void *ring_ptr;
};

} // namespace liburing

#endif // URING_SQ_H
