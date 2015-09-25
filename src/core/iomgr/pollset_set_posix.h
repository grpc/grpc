/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_INTERNAL_CORE_IOMGR_POLLSET_SET_POSIX_H
#define GRPC_INTERNAL_CORE_IOMGR_POLLSET_SET_POSIX_H

#include "src/core/iomgr/fd_posix.h"
#include "src/core/iomgr/pollset_posix.h"

typedef struct grpc_pollset_set {
  gpr_mu mu;

  size_t pollset_count;
  size_t pollset_capacity;
  grpc_pollset **pollsets;

  size_t fd_count;
  size_t fd_capacity;
  grpc_fd **fds;
} grpc_pollset_set;

void grpc_pollset_set_add_fd(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd);
void grpc_pollset_set_del_fd(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd);

#endif /* GRPC_INTERNAL_CORE_IOMGR_POLLSET_WINDOWS_H */
