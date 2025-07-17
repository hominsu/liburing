#ifndef URING_PARAMS_H
#define URING_PARAMS_H

#include <type_traits>

#include "uring/io_uring.h"

namespace liburing {

template <unsigned uring_flags>
struct uring_params final : io_uring_params {
  explicit uring_params() noexcept : io_uring_params{.flags = uring_flags} {}
};

static_assert(std::is_standard_layout_v<uring_params<0>>);

}  // namespace liburing

#endif  // URING_PARAMS_H
