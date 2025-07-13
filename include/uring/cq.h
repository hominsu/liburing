#ifndef URING_CQ_H
#define URING_CQ_H

#include "uring/cqe.h"

namespace liburing {

struct cq {
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

private:
  unsigned *khead;
  unsigned *ktail;
  unsigned *ring_mask;
  unsigned *ring_entries;
  unsigned *kflags;
  unsigned *koverflow;
  cqe *cqes;

  std::size_t ring_sz;
  void *ring_ptr;
};

} // namespace liburing

#endif // URING_CQ_H
