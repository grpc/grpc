/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#define FD_TO_IDX(fd) (-(fd)-1)
#define IDX_TO_FD(idx) (-(idx)-1)

typedef struct cv_node {
  gpr_cv* cv;
  struct cv_node* next;
} cv_node;

typedef struct fd_node {
  int is_set;
  cv_node* cvs;
  struct fd_node* next_free;
} fd_node;

typedef struct cv_fd_table {
  gpr_mu mu;
  int pollcount;
  int shutdown;
  gpr_cv shutdown_complete;
  fd_node* cvfds;
  fd_node* free_fds;
  unsigned int size;
  grpc_poll_function_type poll;
} cv_fd_table;

#endif /* GRPC_CORE_LIB_IOMGR_WAKEUP_FD_CV_H */
