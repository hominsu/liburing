#ifndef URING_SYSCALL_H
#define URING_SYSCALL_H

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>

#ifdef __alpha__
/*
 * alpha and mips are the exceptions, all other architectures have
 * common numbers for new system calls.
 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 535
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 536
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 537
#endif
#elif defined __mips__
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup (__NR_Linux + 425)
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter (__NR_Linux + 426)
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register (__NR_Linux + 427)
#endif
#else /* !__alpha__ and !__mips__ */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif
#endif

/*
 * Don't put this below the #include "arch/$arch/syscall.h", that
 * file may need it.
 */
struct io_uring_params;

static void *ERR_PTR(intptr_t n) { return reinterpret_cast<void *>(n); }

static int PTR_ERR(const void *ptr) {
  return static_cast<int>(reinterpret_cast<intptr_t>(ptr));
}

static bool IS_ERR(const void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) >= -4095UL;
}

#if defined(__x86_64__) || defined(__i386__)
#include "arch/x86/syscall.h"
#elif defined(__aarch64__)
#include "arch/aarch64/syscall.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "arch/riscv64/syscall.h"
#else
/*
 * We don't have native syscall wrappers
 * for this arch. Must use libc!
 */
#ifdef CONFIG_NOLIBC
#error "This arch doesn't support building liburing without libc"
#endif
/* libc syscall wrappers. */
#include "arch/generic/syscall.h"
#endif

#endif  // URING_SYSCALL_H