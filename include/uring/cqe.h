#ifndef URING_CQE_H
#define URING_CQE_H

#include <type_traits>

#include "uring/io_uring.h"

namespace liburing {

using cqe = io_uring_cqe;

static_assert(std::is_standard_layout_v<cqe>);
static_assert(sizeof(cqe) == sizeof(io_uring_cqe));
static_assert(sizeof(cqe) == 16);
static_assert(alignof(cqe) == 8);

}  // namespace liburing

#endif  // URING_CQE_H
