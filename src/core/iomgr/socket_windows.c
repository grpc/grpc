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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINSOCK_SOCKET

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/iomgr/iocp_windows.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/pollset.h"
#include "src/core/iomgr/pollset_windows.h"
#include "src/core/iomgr/socket_windows.h"

grpc_winsocket *grpc_winsocket_create(SOCKET socket) {
  grpc_winsocket *r = gpr_malloc(sizeof(grpc_winsocket));
  memset(r, 0, sizeof(grpc_winsocket));
  r->socket = socket;
  gpr_mu_init(&r->state_mu);
  grpc_iomgr_ref();
  grpc_iocp_add_socket(r);
  return r;
}

/* Schedule a shutdown of the socket operations. Will call the pending
   operations to abort them. We need to do that this way because of the
   various callsites of that function, which happens to be in various
   mutex hold states, and that'd be unsafe to call them directly. */
void grpc_winsocket_shutdown(grpc_winsocket *socket) {
  gpr_mu_lock(&socket->state_mu);
  if (socket->read_info.cb) {
    grpc_iomgr_add_delayed_callback(socket->read_info.cb,
                                    socket->read_info.opaque, 0);
  }
  if (socket->write_info.cb) {
    grpc_iomgr_add_delayed_callback(socket->write_info.cb,
                                    socket->write_info.opaque, 0);
  }
  gpr_mu_unlock(&socket->state_mu);
}

/* Abandons a socket. Either we're going to queue it up for garbage collecting
   from the IO Completion Port thread, or destroy it immediately. Note that this
   mechanisms assumes that we're either always waiting for an operation, or we
   explicitly know that we don't. If there is a future case where we can have
   an "idle" socket which is neither trying to read or write, we'd start leaking
   both memory and sockets. */
void grpc_winsocket_orphan(grpc_winsocket *winsocket) {
  SOCKET socket = winsocket->socket;
  if (winsocket->read_info.outstanding || winsocket->write_info.outstanding) {
    grpc_iocp_socket_orphan(winsocket);
  } else {
    grpc_winsocket_destroy(winsocket);
  }
  closesocket(socket);
  grpc_iomgr_unref();
}

void grpc_winsocket_destroy(grpc_winsocket *winsocket) {
  gpr_mu_destroy(&winsocket->state_mu);
  gpr_free(winsocket);
}

#endif  /* GPR_WINSOCK_SOCKET */
