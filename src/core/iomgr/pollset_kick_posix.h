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

#ifndef GRPC_INTERNAL_CORE_IOMGR_POLLSET_KICK_POSIX_H
#define GRPC_INTERNAL_CORE_IOMGR_POLLSET_KICK_POSIX_H

#include "src/core/iomgr/wakeup_fd_posix.h"
#include <grpc/support/sync.h>

typedef struct grpc_kick_fd_info {
  grpc_wakeup_fd_info wakeup_fd;
  /* used for polling list and free list */
  struct grpc_kick_fd_info *next;
  /* only used when polling */
  struct grpc_kick_fd_info *prev;
} grpc_kick_fd_info;

typedef struct grpc_pollset_kick_state {
  gpr_mu mu;
  int kicked;
  struct grpc_kick_fd_info fd_list;
} grpc_pollset_kick_state;

#define GRPC_POLLSET_KICK_GET_FD(kick_fd_info) GRPC_WAKEUP_FD_GET_READ_FD(&(kick_fd_info)->wakeup_fd)

/* This is an abstraction around the typical pipe mechanism for waking up a
   thread sitting in a poll() style call. */

void grpc_pollset_kick_global_init(void);
void grpc_pollset_kick_global_destroy(void);

void grpc_pollset_kick_init(grpc_pollset_kick_state *kick_state);
void grpc_pollset_kick_destroy(grpc_pollset_kick_state *kick_state);

/* Guarantees a pure posix implementation rather than a specialized one, if
 * applicable. Intended for testing. */
void grpc_pollset_kick_global_init_fallback_fd(void);

/* Must be called before entering poll(). If return value is -1, this consumed
   an existing kick. Otherwise the return value is an FD to add to the poll set.
 */
grpc_kick_fd_info *grpc_pollset_kick_pre_poll(grpc_pollset_kick_state *kick_state);

/* Consume an existing kick. Must be called after poll returns that the fd was
   readable, and before calling kick_post_poll. */
void grpc_pollset_kick_consume(grpc_pollset_kick_state *kick_state, grpc_kick_fd_info *fd_info);

/* Must be called after pre_poll, and after consume if applicable */
void grpc_pollset_kick_post_poll(grpc_pollset_kick_state *kick_state, grpc_kick_fd_info *fd_info);

void grpc_pollset_kick_kick(grpc_pollset_kick_state *kick_state);

#endif  /* GRPC_INTERNAL_CORE_IOMGR_POLLSET_KICK_POSIX_H */
