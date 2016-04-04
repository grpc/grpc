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

#ifndef GRPC_CORE_LIB_IOMGR_POLLSET_WINDOWS_H
#define GRPC_CORE_LIB_IOMGR_POLLSET_WINDOWS_H

#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/socket_windows.h"

/* There isn't really any such thing as a pollset under Windows, due to the
   nature of the IO completion ports. A Windows "pollset" is merely a mutex
   used to synchronize with the IOCP, and workers are condition variables
   used to block threads until work is ready. */

typedef enum {
  GRPC_POLLSET_WORKER_LINK_POLLSET = 0,
  GRPC_POLLSET_WORKER_LINK_GLOBAL,
  GRPC_POLLSET_WORKER_LINK_TYPES
} grpc_pollset_worker_link_type;

typedef struct grpc_pollset_worker_link {
  struct grpc_pollset_worker *next;
  struct grpc_pollset_worker *prev;
} grpc_pollset_worker_link;

struct grpc_pollset;
typedef struct grpc_pollset grpc_pollset;

typedef struct grpc_pollset_worker {
  gpr_cv cv;
  int kicked;
  struct grpc_pollset *pollset;
  grpc_pollset_worker_link links[GRPC_POLLSET_WORKER_LINK_TYPES];
} grpc_pollset_worker;

struct grpc_pollset {
  int shutting_down;
  int kicked_without_pollers;
  int is_iocp_worker;
  grpc_pollset_worker root_worker;
  grpc_closure *on_shutdown;
};

void grpc_pollset_global_init(void);
void grpc_pollset_global_shutdown(void);

#endif /* GRPC_CORE_LIB_IOMGR_POLLSET_WINDOWS_H */
