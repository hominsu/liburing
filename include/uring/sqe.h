#ifndef URING_SQE_H
#define URING_SQE_H

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <cstdint>
#include <span>
#include <type_traits>

#include "uring/compat.h"
#include "uring/io_uring.h"

struct epoll_event;

namespace liburing {

class sqe final : public io_uring_sqe {
 public:
  void set_data(void *data) noexcept {
    this->user_data = reinterpret_cast<uint64_t>(data);
  }
  void set_data(const uint64_t data) noexcept { this->user_data = data; }
  void set_flags(const unsigned flags) noexcept { this->flags = flags; }

  void set_fixed_file() noexcept { this->flags |= IOSQE_FIXED_FILE; }
  void set_io_drain() noexcept { this->flags |= IOSQE_IO_DRAIN; }
  void set_io_link() noexcept { this->flags |= IOSQE_IO_LINK; }
  void set_io_hardlink() noexcept { this->flags |= IOSQE_IO_HARDLINK; }
  void set_async() noexcept { this->flags |= IOSQE_ASYNC; }
  void set_buffer_select() noexcept { this->flags |= IOSQE_BUFFER_SELECT; }
  void set_ceq_skip() noexcept { this->flags |= IOSQE_CQE_SKIP_SUCCESS; }

  void prep_splice(int fd_in, int64_t off_in, int fd_out, int64_t off_out,
                   unsigned int nbytes, unsigned int splice_flags) noexcept {
    prep_rw(IORING_OP_SPLICE, fd_out, nullptr, nbytes,
            static_cast<uint64_t>(off_out));
    this->splice_off_in = static_cast<uint64_t>(off_in);
    this->splice_fd_in = fd_in;
    this->splice_flags = splice_flags;
  }

  void prep_tee(int fd_in, int fd_out, unsigned int nbytes,
                unsigned int splice_flags) noexcept {
    prep_rw(IORING_OP_TEE, fd_out, nullptr, nbytes, 0);
    this->splice_off_in = 0;
    this->splice_fd_in = fd_in;
    this->splice_flags = splice_flags;
  }

  void prep_readv(int fd, const std::span<const iovec> iovecs,
                  uint64_t offset) noexcept {
    prep_rw(IORING_OP_READV, fd, iovecs.data(), iovecs.size(), offset);
  }

  void prep_readv2(int fd, const std::span<const iovec> iovecs, uint64_t offset,
                   int flags) noexcept {
    prep_readv(fd, iovecs, offset);
    this->rw_flags = flags;
  }

  void prep_read_fixed(int fd, std::span<char> buf, uint64_t offset,
                       uint16_t buf_index) noexcept {
    prep_rw(IORING_OP_READ_FIXED, fd, buf.data(), buf.size(), offset);
    this->buf_index = buf_index;
  }

  void prep_readv_fixed(int fd, const std::span<const iovec> iovecs,
                        uint64_t offset, int flags,
                        uint16_t buf_index) noexcept {
    prep_readv2(fd, iovecs, offset, flags);
    this->opcode = IORING_OP_READV_FIXED;
    this->buf_index = buf_index;
  }

  void prep_writev(int fd, const std::span<const iovec> iovecs,
                   uint64_t offset) noexcept {
    prep_rw(IORING_OP_WRITEV, fd, iovecs.data(), iovecs.size(), offset);
  }

  void prep_writev2(int fd, const std::span<const iovec> iovecs,
                    uint64_t offset, int flags) noexcept {
    prep_writev(fd, iovecs, offset);
    this->rw_flags = flags;
  }

  void prep_write_fixed(int fd, const std::span<char> buf, uint64_t offset,
                        uint16_t buf_index) noexcept {
    prep_rw(IORING_OP_WRITE_FIXED, fd, buf.data(), buf.size(), offset);
    this->buf_index = buf_index;
  }

  void prep_writev_fixed(int fd, const std::span<const iovec> iovecs,
                         uint64_t offset, int flags,
                         uint16_t buf_index) noexcept {
    prep_writev2(fd, iovecs, offset, flags);
    this->opcode = IORING_OP_WRITEV_FIXED;
    this->buf_index = buf_index;
  }

  void prep_recvmsg(int fd, msghdr *msg, unsigned flags) noexcept {
    prep_rw(IORING_OP_RECVMSG, fd, msg, 1, 0);
    this->msg_flags = flags;
  }

  void prep_recvmsg_multishot(int fd, msghdr *msg, unsigned flags) noexcept {
    prep_recvmsg(fd, msg, flags);
    this->ioprio |= IORING_RECV_MULTISHOT;
  }

  void prep_sendmsg(int fd, const msghdr *msg, unsigned flags) noexcept {
    prep_rw(IORING_OP_SENDMSG, fd, msg, 1, 0);
    this->msg_flags = flags;
  }

  void prep_poll_add(int fd, unsigned poll_mask) noexcept {
    prep_rw(IORING_OP_POLL_ADD, fd, nullptr, 0, 0);
    this->poll32_events = prep_poll_mask(poll_mask);
  }

  void prep_poll_multishot(int fd, unsigned poll_mask) noexcept {
    prep_poll_add(fd, poll_mask);
    this->len = IORING_POLL_ADD_MULTI;
  }

  void prep_poll_remove(uint64_t user_data) noexcept {
    prep_rw(IORING_OP_POLL_REMOVE, -1, nullptr, 0, 0);
    this->addr = user_data;
  }

  void prep_poll_update(uint64_t old_user_data, uint64_t new_user_data,
                        unsigned poll_mask, unsigned flags) noexcept {
    prep_rw(IORING_OP_POLL_REMOVE, -1, reinterpret_cast<void *>(old_user_data),
            flags, new_user_data);
    this->poll32_events = prep_poll_mask(poll_mask);
  }

  void prep_fsync(int fd, unsigned fsync_flags) noexcept {
    prep_rw(IORING_OP_FSYNC, fd, nullptr, 0, 0);
    this->fsync_flags = fsync_flags;
  }

  void prep_nop() noexcept { prep_rw(IORING_OP_NOP, -1, nullptr, 0, 0); }

  void prep_timeout(__kernel_timespec *ts, unsigned count,
                    unsigned flags) noexcept {
    prep_rw(IORING_OP_TIMEOUT, -1, ts, 1, count);
    this->timeout_flags = flags;
  }

  void prep_timeout_remove(uint64_t user_data, unsigned flags) noexcept {
    prep_rw(IORING_OP_TIMEOUT_REMOVE, -1, nullptr, 0, 0);
    this->addr = user_data;
    this->timeout_flags = flags;
  }

  void prep_timeout_update(__kernel_timespec *ts, uint64_t user_data,
                           unsigned flags) noexcept {
    prep_rw(IORING_OP_TIMEOUT_REMOVE, -1, nullptr, 0,
            reinterpret_cast<uintptr_t>(ts));
    this->addr = user_data;
    this->timeout_flags = flags | IORING_TIMEOUT_UPDATE;
  }

  void prep_accept(int fd, sockaddr *addr, socklen_t *addr_len,
                   int flags) noexcept {
    prep_rw(IORING_OP_ACCEPT, fd, addr, 0,
            reinterpret_cast<uint64_t>(addr_len));
    this->accept_flags = flags;
  }

  void prep_accept_direct(int fd, sockaddr *addr, socklen_t *addr_len,
                          int flags, unsigned int file_index) noexcept {
    prep_accept(fd, addr, addr_len, flags);
    if (file_index == IORING_FILE_INDEX_ALLOC) {
      --file_index;
    }
    set_target_fixed_file(file_index);
  }

  void prep_multishot_accept(int fd, sockaddr *addr, socklen_t *addr_len,
                             int flags) noexcept {
    prep_accept(fd, addr, addr_len, flags);
    this->ioprio |= IORING_ACCEPT_MULTISHOT;
  }

  void prep_multishot_accept_direct(int fd, sockaddr *addr, socklen_t *addr_len,
                                    int flags) noexcept {
    prep_multishot_accept(fd, addr, addr_len, flags);
    set_target_fixed_file(IORING_FILE_INDEX_ALLOC - 1);
  }

  void prep_cancel(void *user_data, int flags) noexcept {
    prep_rw(IORING_OP_ASYNC_CANCEL, -1, user_data, 0, 0);
    this->cancel_flags = flags;
  }

  void prep_cancel_fd(int fd, unsigned int flags) noexcept {
    prep_rw(IORING_OP_ASYNC_CANCEL, fd, nullptr, 0, 0);
    this->cancel_flags = flags | IORING_ASYNC_CANCEL_FD;
  }

  void prep_link_timeout(__kernel_timespec *ts, unsigned flags) noexcept {
    prep_rw(IORING_OP_LINK_TIMEOUT, -1, ts, 1, 0);
    this->timeout_flags = flags;
  }

  void prep_connect(int fd, const sockaddr *addr, socklen_t addr_len) noexcept {
    prep_rw(IORING_OP_CONNECT, fd, addr, 0, addr_len);
  }

  void prep_bind(int fd, sockaddr *addr, socklen_t addr_len) noexcept {
    prep_rw(IORING_OP_BIND, fd, addr, 0, addr_len);
  }

  void prep_listen(int fd, int backlog) noexcept {
    prep_rw(IORING_OP_BIND, fd, nullptr, backlog, 0);
  }

  void prep_files_update(std::span<int> fds, uint64_t offset) noexcept {
    prep_rw(IORING_OP_FILES_UPDATE, -1, fds.data(), fds.size(), offset);
  }

  void prep_fallocate(int fd, int mode, uint64_t offset,
                      uint64_t len) noexcept {
    prep_rw(IORING_OP_FALLOCATE, fd, nullptr, mode, offset);
    this->addr = len;
  }

  void prep_openat(int dfd, const char *path, int flags, mode_t mode) noexcept {
    prep_rw(IORING_OP_OPENAT, dfd, path, mode, 0);
    this->open_flags = flags;
  }

  void prep_openat_direct(int dfd, const char *path, int flags, mode_t mode,
                          unsigned file_index) noexcept {
    prep_openat(dfd, path, flags, mode);
    if (file_index == IORING_FILE_INDEX_ALLOC) {
      --file_index;
    }
    set_target_fixed_file(file_index);
  }

  void prep_open(const char *path, int flags, mode_t mode) noexcept {
    prep_openat(AT_FDCWD, path, flags, mode);
  }

  void prep_open_direct(const char *path, int flags, mode_t mode,
                        unsigned file_index) noexcept {
    prep_openat_direct(AT_FDCWD, path, flags, mode, file_index);
  }

  void prep_close(int fd) noexcept {
    prep_rw(IORING_OP_CLOSE, fd, nullptr, 0, 0);
  }

  void prep_close_direct(unsigned file_index) noexcept {
    prep_close(0);
    set_target_fixed_file(file_index);
  }

  void prep_read(int fd, std::span<char> buf, uint64_t offset) noexcept {
    prep_rw(IORING_OP_READ, fd, buf.data(), buf.size(), offset);
  }

  void prep_read_multishot(int fd, unsigned nbytes, __u64 offset,
                           int buf_group) noexcept {
    prep_rw(IORING_OP_READ_MULTISHOT, fd, nullptr, nbytes, offset);
    this->buf_group = buf_group;
    this->flags = IOSQE_BUFFER_SELECT;
  }

  void prep_write(int fd, std::span<const char> buf, uint64_t offset) noexcept {
    prep_rw(IORING_OP_WRITE, fd, buf.data(), buf.size(), offset);
  }

  void prep_statx(int dfd, const char *path, int flags, unsigned mask,
                  struct statx *statxbuf) noexcept {
    prep_rw(IORING_OP_STATX, dfd, path, mask,
            reinterpret_cast<uint64_t>(statxbuf));
    this->statx_flags = static_cast<uint32_t>(flags);
  }

  void prep_fadvise(int fd, uint64_t offset, uint32_t len,
                    int advice) noexcept {
    prep_rw(IORING_OP_FADVISE, fd, nullptr, len, offset);
    this->fadvise_advice = static_cast<uint32_t>(advice);
  }

  void prep_fadvise(int fd, uint64_t offset, off_t len, int advice) noexcept {
    prep_rw(IORING_OP_FADVISE, fd, nullptr, 0, offset);
    this->addr = len;
    this->fadvise_advice = static_cast<uint32_t>(advice);
  }

  void prep_madvise(void *addr, uint32_t length, int advice) noexcept {
    prep_rw(IORING_OP_MADVISE, -1, addr, length, 0);
    this->fadvise_advice = static_cast<uint32_t>(advice);
  }

  void prep_madvise(void *addr, off_t length, int advice) noexcept {
    prep_rw(IORING_OP_MADVISE, -1, addr, 0, length);
    this->fadvise_advice = static_cast<uint32_t>(advice);
  }

  void prep_send(int sockfd, std::span<const char> buf, int flags) noexcept {
    prep_rw(IORING_OP_SEND, sockfd, buf.data(), buf.size(), 0);
    this->msg_flags = static_cast<uint32_t>(flags);
  }

  void prep_send_bundle(int sockfd, size_t len, int flags) noexcept {
    prep_rw(IORING_OP_SEND, sockfd, nullptr, len, 0);
    this->msg_flags = static_cast<uint32_t>(flags);
    this->ioprio |= IORING_RECVSEND_BUNDLE;
  }

  void prep_send_set_addr(const sockaddr *dest_addr,
                          uint16_t addr_len) noexcept {
    this->addr2 = reinterpret_cast<uintptr_t>(dest_addr);
    this->addr_len = addr_len;
  }

  void prep_sendto(int sockfd, std::span<const char> buf, int flags,
                   const sockaddr *addr, socklen_t addr_len) noexcept {
    prep_send(sockfd, buf, flags);
    prep_send_set_addr(addr, addr_len);
  }

  void prep_send_zc(int sockfd, std::span<const char> buf, int flags,
                    unsigned zc_flags) noexcept {
    prep_rw(IORING_OP_SEND_ZC, sockfd, buf.data(), buf.size(), 0);
    this->msg_flags = static_cast<uint32_t>(flags);
    this->ioprio = zc_flags;
  }

  void prep_send_zc_fixed(int sockfd, std::span<const char> buf, int flags,
                          unsigned zc_flags, unsigned buf_index) noexcept {
    prep_send_zc(sockfd, buf, flags, zc_flags);
    this->ioprio |= IORING_RECVSEND_FIXED_BUF;
    this->buf_index |= buf_index;
  }

  void prep_sendmsg_zc(int fd, const msghdr *msg, unsigned flags) noexcept {
    prep_sendmsg(fd, msg, flags);
    this->opcode = IORING_OP_SENDMSG_ZC;
  }

  void prep_sendmsg_zc_fixed(int fd, const msghdr *msg, unsigned flags,
                             unsigned buf_index) noexcept {
    prep_sendmsg_zc(fd, msg, flags);
    this->ioprio |= IORING_RECVSEND_FIXED_BUF;
    this->buf_index = buf_index;
  }

  void prep_recv(int sockfd, std::span<char> buf, int flags) {
    prep_rw(IORING_OP_RECV, sockfd, buf.data(), buf.size(), 0);
    this->msg_flags = static_cast<uint32_t>(flags);
  }

  void prep_recv_multishot(int sockfd, std::span<char> buf,
                           int flags) noexcept {
    prep_recv(sockfd, buf, flags);
    this->ioprio |= IORING_RECV_MULTISHOT;
  }

#ifdef HAVE_OPEN_HOW
  void prep_openat2(int dfd, const char *path, open_how *how) noexcept {
    prep_rw(IORING_OP_OPENAT2, dfd, path, sizeof(*how),
            reinterpret_cast<uintptr_t>(how));
  }

  void prep_openat2_direct(int dfd, const char *path, open_how *how,
                           unsigned file_index) noexcept {
    prep_openat2(dfd, path, how);
    if (file_index == IORING_FILE_INDEX_ALLOC) {
      --file_index;
    }
    set_target_fixed_file(file_index);
  }
#endif

  void prep_epoll_ctl(int epfd, int fd, int op, epoll_event *ev) noexcept {
    prep_rw(IORING_OP_EPOLL_CTL, epfd, ev, static_cast<uint32_t>(op),
            static_cast<uint32_t>(fd));
  }

  void prep_provide_buffers(std::span<const char> buf, int nr, int bgid,
                            int bid) noexcept {
    prep_rw(IORING_OP_PROVIDE_BUFFERS, nr, buf.data(), buf.size(),
            static_cast<uint64_t>(bid));
    this->buf_group = static_cast<uint16_t>(bgid);
  }

  void prep_remove_buffers(int nr, int bgid) noexcept {
    prep_rw(IORING_OP_REMOVE_BUFFERS, nr, nullptr, 0, 0);
    this->buf_group = static_cast<uint16_t>(bgid);
  }

  void prep_shutdown(int fd, int how) noexcept {
    prep_rw(IORING_OP_SHUTDOWN, fd, nullptr, static_cast<uint32_t>(how), 0);
  }

  void prep_unlinkat(int dfd, const char *path, int flags) noexcept {
    prep_rw(IORING_OP_UNLINKAT, dfd, path, 0, 0);
    this->unlink_flags = static_cast<uint32_t>(flags);
  }

  void prep_unlink(const char *path, int flags) noexcept {
    prep_unlinkat(AT_FDCWD, path, flags);
  }

  void prep_renameat(int olddfd, const char *oldpath, int newdfd,
                     const char *newpath, int flags) noexcept {
    prep_rw(IORING_OP_RENAMEAT, olddfd, oldpath, static_cast<uint32_t>(newdfd),
            reinterpret_cast<uintptr_t>(newpath));
    this->rename_flags = static_cast<uint32_t>(flags);
  }

  void prep_rename(const char *oldpath, const char *newpath) noexcept {
    prep_renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
  }

  void prep_sync_file_range(int fd, uint32_t len, uint64_t offset,
                            int flags) noexcept {
    prep_rw(IORING_OP_SYNC_FILE_RANGE, fd, nullptr, len, offset);
    this->sync_range_flags = static_cast<uint32_t>(flags);
  }

  void prep_mkdirat(int dfd, const char *path, mode_t mode) noexcept {
    prep_rw(IORING_OP_MKDIRAT, dfd, path, mode, 0);
  }

  void prep_mkdir(const char *path, mode_t mode) noexcept {
    prep_mkdirat(AT_FDCWD, path, mode);
  }

  void prep_symlinkat(const char *target, int newdirfd,
                      const char *linkpath) noexcept {
    prep_rw(IORING_OP_SYMLINKAT, newdirfd, target, 0,
            reinterpret_cast<uintptr_t>(linkpath));
  }

  void prep_symlink(const char *target, const char *linkpath) noexcept {
    prep_symlinkat(target, AT_FDCWD, linkpath);
  }

  void prep_linkat(int olddfd, const char *oldpath, int newdfd,
                   const char *newpath, int flags) noexcept {
    prep_rw(IORING_OP_LINKAT, olddfd, oldpath, static_cast<uint32_t>(newdfd),
            reinterpret_cast<uintptr_t>(newpath));
    this->hardlink_flags = static_cast<uint32_t>(flags);
  }

  void prep_link(const char *oldpath, const char *newpath, int flags) noexcept {
    prep_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, flags);
  }

  void prep_msg_ring_cqe_flags(int fd, uint32_t len, uint64_t data,
                               uint32_t flags, uint32_t cqe_flags) noexcept {
    prep_rw(IORING_OP_MSG_RING, fd, nullptr, len, data);
    this->msg_ring_flags = IORING_MSG_RING_FLAGS_PASS | flags;
    this->file_index = cqe_flags;
  }

  void prep_msg_ring(int fd, uint32_t cqe_res, uint64_t cqe_user_data,
                     uint32_t flags) noexcept {
    prep_rw(IORING_OP_MSG_RING, fd, nullptr, cqe_res, cqe_user_data);
    this->msg_ring_flags = flags;
  }

  void prep_msg_ring_fd(int fd, int source_fd, int target_fd, uint64_t data,
                        uint32_t flags) noexcept {
    prep_rw(IORING_OP_MSG_RING, fd,
            reinterpret_cast<void *>(IORING_MSG_SEND_FD), 0, data);
    addr3 = source_fd;
    if (static_cast<unsigned int>(target_fd) == IORING_FILE_INDEX_ALLOC) {
      --target_fd;
    }
    set_target_fixed_file(target_fd);
    msg_ring_flags = flags;
  }

  void prep_msg_ring_fd_alloc(int fd, int source_fd, uint64_t data,
                              uint32_t flags) noexcept {
    prep_msg_ring_fd(fd, source_fd, IORING_FILE_INDEX_ALLOC, data, flags);
  }

  void prep_getxattr(const char *name, char *value, const char *path,
                     unsigned int len) noexcept {
    prep_rw(IORING_OP_GETXATTR, 0, name, len,
            reinterpret_cast<uintptr_t>(value));
    this->addr3 = reinterpret_cast<uintptr_t>(path);
    this->xattr_flags = 0;
  }

  void prep_setxattr(const char *name, char *value, const char *path, int flags,
                     unsigned int len) noexcept {
    prep_rw(IORING_OP_SETXATTR, 0, name, len,
            reinterpret_cast<uintptr_t>(value));
    this->addr3 = reinterpret_cast<uintptr_t>(path);
    this->xattr_flags = flags;
  }

  void prep_fgetxattr(int fd, const char *name, char *value,
                      unsigned int len) noexcept {
    prep_rw(IORING_OP_FGETXATTR, fd, name, len,
            reinterpret_cast<uintptr_t>(value));
    this->xattr_flags = 0;
  }

  void prep_fsetxattr(int fd, const char *name, const char *value, int flags,
                      unsigned int len) noexcept {
    prep_rw(IORING_OP_FSETXATTR, fd, name, len,
            reinterpret_cast<uintptr_t>(value));
    this->xattr_flags = flags;
  }

  void prep_socket(int domain, int type, int protocol,
                   unsigned int flags) noexcept {
    prep_rw(IORING_OP_SOCKET, domain, nullptr, protocol, type);
    this->rw_flags = static_cast<int>(flags);
  }

  void prep_socket_direct(int domain, int type, int protocol,
                          unsigned file_index, unsigned int flags) noexcept {
    prep_rw(IORING_OP_SOCKET, domain, nullptr, protocol, type);
    this->rw_flags = static_cast<int>(flags);
    if (file_index == IORING_FILE_INDEX_ALLOC) {
      --file_index;
    }
    set_target_fixed_file(file_index);
  }

  void prep_socket_direct_alloc(int domain, int type, int protocol,
                                unsigned int flags) noexcept {
    prep_socket_direct(domain, type, protocol, IORING_FILE_INDEX_ALLOC - 1,
                       flags);
  }

 private:
  void set_target_fixed_file(unsigned int file_index) noexcept {
    this->file_index = file_index + 1;
  }

  void prep_rw(int op, int fd, const void *addr, unsigned len,
               uint64_t offset) noexcept {
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

static_assert(std::is_standard_layout_v<sqe>);
static_assert(sizeof(sqe) == sizeof(io_uring_sqe));
static_assert(sizeof(sqe) == 64);
static_assert(alignof(sqe) == 8);

}  // namespace liburing

#endif  // URING_SQE_H
