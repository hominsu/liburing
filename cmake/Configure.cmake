include(CheckCSourceRuns)

check_c_source_runs([[
    #include <linux/fs.h>
    int main(const int argc, char *argv[]) {
      __kernel_rwf_t x;
      x = 0;
      return x;
    }
    ]] HAVE_KERNEL_RWF_T
)

check_c_source_runs([[
    #include <linux/time.h>
    #include <linux/time_types.h>
    int main(const int argc, char *argv[]) {
      struct __kernel_timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 1;
      return 0;
    }
    ]] HAVE_KERNEL_TIMESPEC
)

check_c_source_runs([[
    #include <sys/types.h>
    #include <fcntl.h>
    #include <string.h>
    #include <linux/openat2.h>
    int main(const int argc, char *argv[]) {
      struct open_how how;
      how.flags = 0;
      how.mode = 0;
      how.resolve = 0;
      return 0;
    }
    ]] HAVE_OPEN_HOW
)

configure_file(
        ${PROJECT_SOURCE_DIR}/include/uring/compat.h.in
        ${PROJECT_BINARY_DIR}/include/uring/compat.h
)

include_directories(${PROJECT_BINARY_DIR}/include)
