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
 * A condition variable (cv) wakeup fd is represented as a negative number.
 * The poll() function is overriden to allow polling on these cv wakeup fds.
 * When we poll a cv wakeup fd, we create a condition variable.  This condition
 * variable is then stored in a sharded global table, and indexed by the wakeup
 * fd.
 * The non-wakeup (socket) fds are then poll()ed in a background thread, while
 * the
 * main thread waits on the condition variable.  The condition variable can be
 * triggered by either the socket poll() returning, or a wakeup fd being set.
 */

#ifndef GRPC_CORE_LIB_IOMGR_WAKEUP_FD_CV_H
#define GRPC_CORE_LIB_IOMGR_WAKEUP_FD_CV_H

#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/ev_posix.h"

#define GRPC_POLLCV_TABLE_SHARDS 16
#define GRPC_POLLCV_MAX_SHARD_SIZE ((1 << 30) / GRPC_POLLCV_TABLE_SHARDS)

#define GRPC_POLLCV_FD_TO_SHARD(fd) ((-(fd)-1) / GRPC_POLLCV_MAX_SHARD_SIZE)
#define GRPC_POLLCV_FD_TO_IDX(fd) ((-(fd)-1) % GRPC_POLLCV_MAX_SHARD_SIZE)
#define GRPC_POLLCV_SHARD_IDX_TO_FD(shard, idx) \
  (-(shard * GRPC_POLLCV_MAX_SHARD_SIZE + idx) - 1)

typedef struct cv_node {
  gpr_cv* cv;
  gpr_mu* mu;
  struct cv_node* next;
} cv_node;

typedef struct fd_node {
  int is_set;
  cv_node* cvs;
  struct fd_node* next_free;
} fd_node;

typedef struct cv_fd_table {
  gpr_mu mu;
  fd_node* cvfds;
  fd_node* free_fds;
  int size;
} cv_fd_table;

#endif /* GRPC_CORE_LIB_IOMGR_WAKEUP_FD_CV_H */
