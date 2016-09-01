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

#include <winsock2.h>

// must be included after winsock2.h
#include <mswsock.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"

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
void grpc_winsocket_shutdown(grpc_winsocket *winsocket) {
  /* Grab the function pointer for DisconnectEx for that specific socket.
     It may change depending on the interface. */
  int status;
  GUID guid = WSAID_DISCONNECTEX;
  LPFN_DISCONNECTEX DisconnectEx;
  DWORD ioctl_num_bytes;

  status = WSAIoctl(winsocket->socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guid, sizeof(guid), &DisconnectEx, sizeof(DisconnectEx),
                    &ioctl_num_bytes, NULL, NULL);

  if (status == 0) {
    DisconnectEx(winsocket->socket, NULL, 0, 0);
  } else {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "Unable to retrieve DisconnectEx pointer : %s",
            utf8_message);
    gpr_free(utf8_message);
  }
  closesocket(winsocket->socket);
}

static void destroy(grpc_winsocket *winsocket) {
  grpc_iomgr_unregister_object(&winsocket->iomgr_object);
  gpr_mu_destroy(&winsocket->state_mu);
  gpr_free(winsocket);
}

static bool check_destroyable(grpc_winsocket *winsocket) {
  return winsocket->destroy_called == true &&
         winsocket->write_info.closure == NULL &&
         winsocket->read_info.closure == NULL;
}

void grpc_winsocket_destroy(grpc_winsocket *winsocket) {
  gpr_mu_lock(&winsocket->state_mu);
  GPR_ASSERT(!winsocket->destroy_called);
  winsocket->destroy_called = true;
  bool should_destroy = check_destroyable(winsocket);
  gpr_mu_unlock(&winsocket->state_mu);
  if (should_destroy) destroy(winsocket);
}

/* Calling notify_on_read or write means either of two things:
-) The IOCP already completed in the background, and we need to call
the callback now.
-) The IOCP hasn't completed yet, and we're queuing it for later. */
static void socket_notify_on_iocp(grpc_exec_ctx *exec_ctx,
                                  grpc_winsocket *socket, grpc_closure *closure,
                                  grpc_winsocket_callback_info *info) {
  GPR_ASSERT(info->closure == NULL);
  gpr_mu_lock(&socket->state_mu);
  if (info->has_pending_iocp) {
    info->has_pending_iocp = 0;
    grpc_exec_ctx_sched(exec_ctx, closure, GRPC_ERROR_NONE, NULL);
  } else {
    info->closure = closure;
  }
  gpr_mu_unlock(&socket->state_mu);
}

void grpc_socket_notify_on_write(grpc_exec_ctx *exec_ctx,
                                 grpc_winsocket *socket,
                                 grpc_closure *closure) {
  socket_notify_on_iocp(exec_ctx, socket, closure, &socket->write_info);
}

void grpc_socket_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_winsocket *socket,
                                grpc_closure *closure) {
  socket_notify_on_iocp(exec_ctx, socket, closure, &socket->read_info);
}

void grpc_socket_become_ready(grpc_exec_ctx *exec_ctx, grpc_winsocket *socket,
                              grpc_winsocket_callback_info *info) {
  GPR_ASSERT(!info->has_pending_iocp);
  gpr_mu_lock(&socket->state_mu);
  if (info->closure) {
    grpc_exec_ctx_sched(exec_ctx, info->closure, GRPC_ERROR_NONE, NULL);
    info->closure = NULL;
  } else {
    info->has_pending_iocp = 1;
  }
  bool should_destroy = check_destroyable(socket);
  gpr_mu_unlock(&socket->state_mu);
  if (should_destroy) destroy(socket);
}

#endif /* GPR_WINSOCK_SOCKET */
