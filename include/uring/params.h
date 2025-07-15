#ifndef URING_PARAMS_H
#define URING_PARAMS_H

#include "uring/io_uring.h"

#include <type_traits>

namespace liburing {

struct uring_params final : io_uring_params {
  uring_params() noexcept : io_uring_params{} {}

  explicit uring_params(const unsigned flags) noexcept
      : io_uring_params{.flags = flags} {}
};

static_assert(std::is_standard_layout_v<uring_params>);

} // namespace liburing

#endif // URING_PARAMS_H
