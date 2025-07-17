#include <cstdio>

#include "uring/cqe.h"
#include "uring/sqe.h"
#include "uring/uring.h"

#define QD 4

int main(const int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "%s: file\n", argv[0]);
    return 1;
  }

  liburing::uring ring;
  ring.init(QD);

  return 0;
}
