/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * wakeup_fd_cv uses condition variables to implement wakeup fds.
 *
 * It is intended for use only in cases when eventfd() and pipe() are not
 * available.  It can only be used with the "poll" engine.
 *
 * Implementation:
 * A global table of cv wakeup fds is mantained.  A cv wakeup fd is a negative
 * file descriptor.  poll() is then run in a background thread with only the
 * real socket fds while we wait on a condition variable trigged by either the
 * poll() completion or a wakeup_fd() call.
 *
 */

#ifndef GRPC_CORE_LIB_IOMGR_WAKEUP_FD_CV_H
#define GRPC_CORE_LIB_IOMGR_WAKEUP_FD_CV_H

#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/ev_posix.h"

#define GRPC_FD_TO_IDX(fd) (-(fd)-1)
#define GRPC_IDX_TO_FD(idx) (-(idx)-1)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cv_node {
  gpr_cv* cv;
  struct cv_node* next;
  struct cv_node* prev;
} cv_node;

typedef struct fd_node {
  int is_set;
  cv_node* cvs;
  struct fd_node* next_free;
} fd_node;

typedef struct cv_fd_table {
  gpr_mu mu;
  gpr_refcount pollcount;
  gpr_cv shutdown_cv;
  fd_node* cvfds;
  fd_node* free_fds;
  unsigned int size;
  grpc_poll_function_type poll;
} cv_fd_table;

extern const grpc_wakeup_fd_vtable grpc_cv_wakeup_fd_vtable;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_WAKEUP_FD_CV_H */
