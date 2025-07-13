#ifndef URING_URING_H
#define URING_URING_H

#include "uring/cq.h"
#include "uring/io_uring.h"
#include "uring/sq.h"

#include <type_traits>

namespace liburing {

struct uring_params final : io_uring_params {
  uring_params() noexcept : io_uring_params{} {}

  explicit uring_params(const unsigned flags) noexcept
      : io_uring_params{.flags = flags} {}
};

static_assert(std::is_standard_layout_v<uring_params>,
              "Must maintain C-compatible layout");

template <unsigned flags> class uring {};

} // namespace liburing

#endif // URING_URING_H
