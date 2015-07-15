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
#include <grpc/support/string_util.h>

#include "src/core/iomgr/iocp_windows.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/pollset.h"
#include "src/core/iomgr/pollset_windows.h"
#include "src/core/iomgr/socket_windows.h"

grpc_winsocket *grpc_winsocket_create(SOCKET socket, const char *name) {
  char *final_name;
  grpc_winsocket *r = gpr_malloc(sizeof(grpc_winsocket));
  memset(r, 0, sizeof(grpc_winsocket));
  r->socket = socket;
  gpr_mu_init(&r->state_mu);
  gpr_asprintf(&final_name, "%s:socket=0x%p", name, r);
  grpc_iomgr_register_object(&r->iomgr_object, final_name);
  gpr_free(final_name);
  grpc_iocp_add_socket(r);
  return r;
}

/* Schedule a shutdown of the socket operations. Will call the pending
   operations to abort them. We need to do that this way because of the
   various callsites of that function, which happens to be in various
   mutex hold states, and that'd be unsafe to call them directly. */
int grpc_winsocket_shutdown(grpc_winsocket *winsocket) {
  int callbacks_set = 0;
  SOCKET socket;
  gpr_mu_lock(&winsocket->state_mu);
  socket = winsocket->socket;
  if (winsocket->read_info.cb) {
    callbacks_set++;
    grpc_iomgr_closure_init(&winsocket->shutdown_closure,
                            winsocket->read_info.cb,
                            winsocket->read_info.opaque);
    grpc_iomgr_add_delayed_callback(&winsocket->shutdown_closure, 0);
  }
  if (winsocket->write_info.cb) {
    callbacks_set++;
    grpc_iomgr_closure_init(&winsocket->shutdown_closure,
                            winsocket->write_info.cb,
                            winsocket->write_info.opaque);
    grpc_iomgr_add_delayed_callback(&winsocket->shutdown_closure, 0);
  }
  gpr_mu_unlock(&winsocket->state_mu);
  closesocket(socket);
  return callbacks_set;
}

/* Abandons a socket. Either we're going to queue it up for garbage collecting
   from the IO Completion Port thread, or destroy it immediately. Note that this
   mechanisms assumes that we're either always waiting for an operation, or we
   explicitly know that we don't. If there is a future case where we can have
   an "idle" socket which is neither trying to read or write, we'd start leaking
   both memory and sockets. */
void grpc_winsocket_orphan(grpc_winsocket *winsocket) {
  grpc_iomgr_unregister_object(&winsocket->iomgr_object);
  if (winsocket->read_info.outstanding || winsocket->write_info.outstanding) {
    grpc_iocp_socket_orphan(winsocket);
  } else {
    grpc_winsocket_destroy(winsocket);
  }
}

void grpc_winsocket_destroy(grpc_winsocket *winsocket) {
  gpr_mu_destroy(&winsocket->state_mu);
  gpr_free(winsocket);
}

#endif  /* GPR_WINSOCK_SOCKET */
