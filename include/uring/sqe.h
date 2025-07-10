#ifndef URING_SQE_H
#define URING_SQE_H

#include "uring/io_uring.h"

#include <cstdint>
#include <span>
#include <type_traits>

#include <sys/socket.h>

namespace liburing {

class sqe final : io_uring_sqe {
public:
  void set_data(const uint64_t data) noexcept { this->user_data = data; }
  void set_flags(const unsigned flags) noexcept { this->flags = flags; }

  void set_fixed_file() noexcept { this->flags |= IOSQE_FIXED_FILE; }
  void set_io_drain() noexcept { this->flags |= IOSQE_IO_DRAIN; }
  void set_io_link() noexcept { this->flags |= IOSQE_IO_LINK; }
  void set_io_hardlink() noexcept { this->flags |= IOSQE_IO_HARDLINK; }
  void set_async() noexcept { this->flags |= IOSQE_ASYNC; }
  void set_buffer_select() noexcept { this->flags |= IOSQE_BUFFER_SELECT; }
  void set_ceq_skip() noexcept { this->flags |= IOSQE_CQE_SKIP_SUCCESS; }

  void prep_splice(const int fd_in, const int64_t off_in, const int fd_out,
                   const int64_t off_out, const unsigned int nbytes,
                   const unsigned int splice_flags) noexcept {
    prep_rw(IORING_OP_SPLICE, fd_out, nullptr, nbytes,
            static_cast<uint64_t>(off_out));
    this->splice_off_in = static_cast<uint64_t>(off_in);
    this->splice_fd_in = fd_in;
    this->splice_flags = splice_flags;
  }

  void prep_tee(const int fd_in, const int fd_out, const unsigned int nbytes,
                const unsigned int splice_flags) noexcept {
    prep_rw(IORING_OP_TEE, fd_out, nullptr, nbytes, 0);
    this->splice_off_in = 0;
    this->splice_fd_in = fd_in;
    this->splice_flags = splice_flags;
  }

  void prep_readv(const int fd, const std::span<const iovec> iovecs,
                  const uint64_t offset) noexcept {
    return prep_rw(IORING_OP_READV, fd, iovecs.data(), iovecs.size(), offset);
  }

  void prep_readv2(const int fd, const std::span<const iovec> iovecs,
                   const uint64_t offset, const int flags) noexcept {
    prep_readv(fd, iovecs, offset);
    this->rw_flags = flags;
  }

  void prep_read_fixed(const int fd, const std::span<char> buf,
                       const uint64_t offset,
                       const uint16_t buf_index) noexcept {
    prep_rw(IORING_OP_READ_FIXED, fd, buf.data(), buf.size(), offset);
    this->buf_index = buf_index;
  }

  void prep_readv_fixed(const int fd, const std::span<const iovec> iovecs,
                        const uint64_t offset, const int flags,
                        const uint16_t buf_index) noexcept {
    prep_readv2(fd, iovecs, offset, flags);
    this->opcode = IORING_OP_READV_FIXED;
    this->buf_index = buf_index;
  }

  void prep_writev(const int fd, const std::span<const iovec> iovecs,
                   const uint64_t offset) noexcept {
    prep_rw(IORING_OP_WRITEV, fd, iovecs.data(), iovecs.size(), offset);
  }

  void prep_writev2(const int fd, const std::span<const iovec> iovecs,
                    const uint64_t offset, const int flags) noexcept {
    prep_writev(fd, iovecs, offset);
    this->rw_flags = flags;
  }

  void prep_write_fixed(const int fd, const std::span<char> buf,
                        const uint64_t offset,
                        const uint16_t buf_index) noexcept {
    prep_rw(IORING_OP_WRITE_FIXED, fd, buf.data(), buf.size(), offset);
    this->buf_index = buf_index;
  }

  void prep_writev_fixed(const int fd, const std::span<const iovec> iovecs,
                         const uint64_t offset, const int flags,
                         const uint16_t buf_index) noexcept {
    prep_writev2(fd, iovecs, offset, flags);
    this->opcode = IORING_OP_WRITEV_FIXED;
    this->buf_index = buf_index;
  }

  void prep_recvmsg(const int fd, msghdr *msg, const unsigned flags) noexcept {
    prep_rw(IORING_OP_RECVMSG, fd, msg, 1, 0);
    this->msg_flags = flags;
  }

  void prep_recvmsg_multishot(const int fd, msghdr *msg,
                              const unsigned flags) noexcept {
    prep_recvmsg(fd, msg, flags);
    this->ioprio |= IORING_RECV_MULTISHOT;
  }

  void prep_sendmsg(const int fd, const msghdr *msg,
                    const unsigned flags) noexcept {
    prep_rw(IORING_OP_SENDMSG, fd, msg, 1, 0);
    this->msg_flags = flags;
  }

  void prep_poll_add(const int fd, const unsigned poll_mask) noexcept {
    prep_rw(IORING_OP_POLL_ADD, fd, nullptr, 0, 0);
    this->poll32_events = prep_poll_mask(poll_mask);
  }

  void prep_poll_multishot(const int fd, const unsigned poll_mask) noexcept {
    prep_poll_add(fd, poll_mask);
    this->len = IORING_POLL_ADD_MULTI;
  }

  void prep_poll_remove(const uint64_t user_data) noexcept {
    prep_rw(IORING_OP_POLL_REMOVE, -1, nullptr, 0, 0);
    this->addr = user_data;
  }

  void prep_poll_update(const uint64_t old_user_data,
                        const uint64_t new_user_data, unsigned poll_mask,
                        const unsigned flags) noexcept {
    prep_rw(IORING_OP_POLL_REMOVE, -1, nullptr, flags, new_user_data);
    this->addr = old_user_data;
    this->poll32_events = prep_poll_mask(poll_mask);
  }

  void prep_fsync(const int fd, const unsigned fsync_flags) noexcept {
    prep_rw(IORING_OP_FSYNC, fd, nullptr, 0, 0);
    this->fsync_flags = fsync_flags;
  }

  void prep_nop() noexcept { prep_rw(IORING_OP_NOP, -1, nullptr, 0, 0); }

private:
  void set_target_fixed_file(const unsigned int file_index) noexcept {
    this->file_index = file_index + 1;
  }

  void prep_rw(const int op, const int fd, const void *addr, const unsigned len,
               const uint64_t offset) noexcept {
    this->opcode = op;
    this->flags = 0;
    this->ioprio = 0;
    this->fd = fd;
    this->off = offset;
    this->addr = reinterpret_cast<uint64_t>(addr);
    this->len = len;
    this->rw_flags = 0;
    this->buf_index = 0;
    this->personality = 0;
    this->file_index = 0;
    this->addr3 = 0;
    this->__pad2[0] = 0;
  }

  static unsigned prep_poll_mask(unsigned poll_mask) noexcept {
#if __BYTE_ORDER == __BIG_ENDIAN
    poll_mask = __swahw32(poll_mask);
#endif
    return poll_mask;
  }
};

static_assert(std::is_standard_layout_v<io_uring_sqe>,
              "Must maintain C-compatible layout");

} // namespace liburing

#endif // URING_SQE_H
