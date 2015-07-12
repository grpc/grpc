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

#include <grpc/support/log.h>
#include <grpc/support/log_win32.h>
#include <grpc/support/alloc.h>
#include <grpc/support/thd.h>

#include "src/core/iomgr/alarm_internal.h"
#include "src/core/iomgr/iocp_windows.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/socket_windows.h"

static ULONG g_iocp_kick_token;
static OVERLAPPED g_iocp_custom_overlap;

static gpr_event g_shutdown_iocp;
static gpr_event g_iocp_done;
static gpr_atm g_orphans = 0;
static gpr_atm g_custom_events = 0;

static HANDLE g_iocp;

static void do_iocp_work() {
  BOOL success;
  DWORD bytes = 0;
  DWORD flags = 0;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;
  grpc_winsocket *socket;
  grpc_winsocket_callback_info *info;
  void(*f)(void *, int) = NULL;
  void *opaque = NULL;
  success = GetQueuedCompletionStatus(g_iocp, &bytes,
                                      &completion_key, &overlapped,
                                      INFINITE);
  /* success = 0 and overlapped = NULL means the deadline got attained.
     Which is impossible. since our wait time is +inf */
  GPR_ASSERT(success || overlapped);
  GPR_ASSERT(completion_key && overlapped);
  if (overlapped == &g_iocp_custom_overlap) {
    gpr_atm_full_fetch_add(&g_custom_events, -1);
    if (completion_key == (ULONG_PTR) &g_iocp_kick_token) {
      /* We were awoken from a kick. */
      return;
    }
    gpr_log(GPR_ERROR, "Unknown custom completion key.");
    abort();
  }

  socket = (grpc_winsocket*) completion_key;
  if (overlapped == &socket->write_info.overlapped) {
    info = &socket->write_info;
  } else if (overlapped == &socket->read_info.overlapped) {
    info = &socket->read_info;
  } else {
    gpr_log(GPR_ERROR, "Unknown IOCP operation");
    abort();
  }
  GPR_ASSERT(info->outstanding);
  if (socket->orphan) {
    info->outstanding = 0;
    if (!socket->read_info.outstanding && !socket->write_info.outstanding) {
      grpc_winsocket_destroy(socket);
      gpr_atm_full_fetch_add(&g_orphans, -1);
    }
    return;
  }
  success = WSAGetOverlappedResult(socket->socket, &info->overlapped, &bytes,
                                   FALSE, &flags);
  info->bytes_transfered = bytes;
  info->wsa_error = success ? 0 : WSAGetLastError();
  GPR_ASSERT(overlapped == &info->overlapped);
  gpr_mu_lock(&socket->state_mu);
  GPR_ASSERT(!info->has_pending_iocp);
  if (info->cb) {
    f = info->cb;
    opaque = info->opaque;
    info->cb = NULL;
  } else {
    info->has_pending_iocp = 1;
  }
  gpr_mu_unlock(&socket->state_mu);
  if (f) f(opaque, 1);
}

static void iocp_loop(void *p) {
  while (gpr_atm_acq_load(&g_orphans) ||
         gpr_atm_acq_load(&g_custom_events) ||
         !gpr_event_get(&g_shutdown_iocp)) {
    grpc_maybe_call_delayed_callbacks(NULL, 1);
    do_iocp_work();
  }

  gpr_event_set(&g_iocp_done, (void *)1);
}

void grpc_iocp_init(void) {
  gpr_thd_id id;

  g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                  NULL, (ULONG_PTR)NULL, 0);
  GPR_ASSERT(g_iocp);

  gpr_event_init(&g_iocp_done);
  gpr_event_init(&g_shutdown_iocp);
  gpr_thd_new(&id, iocp_loop, NULL, NULL);
}

void grpc_iocp_kick(void) {
  BOOL success;

  gpr_atm_full_fetch_add(&g_custom_events, 1);
  success = PostQueuedCompletionStatus(g_iocp, 0,
                                       (ULONG_PTR) &g_iocp_kick_token,
                                       &g_iocp_custom_overlap);
  GPR_ASSERT(success);
}

void grpc_iocp_shutdown(void) {
  BOOL success;
  gpr_event_set(&g_shutdown_iocp, (void *)1);
  grpc_iocp_kick();
  gpr_event_wait(&g_iocp_done, gpr_inf_future);
  success = CloseHandle(g_iocp);
  GPR_ASSERT(success);
}

void grpc_iocp_add_socket(grpc_winsocket *socket) {
  HANDLE ret;
  if (socket->added_to_iocp) return;
  ret = CreateIoCompletionPort((HANDLE)socket->socket,
                               g_iocp, (gpr_uintptr) socket, 0);
  if (!ret) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "Unable to add socket to iocp: %s", utf8_message);
    gpr_free(utf8_message);
    __debugbreak();
    abort();
  }
  socket->added_to_iocp = 1;
  GPR_ASSERT(ret == g_iocp);
}

void grpc_iocp_socket_orphan(grpc_winsocket *socket) {
  GPR_ASSERT(!socket->orphan);
  gpr_atm_full_fetch_add(&g_orphans, 1);
  socket->orphan = 1;
}

/* Calling notify_on_read or write means either of two things:
   -) The IOCP already completed in the background, and we need to call
   the callback now.
   -) The IOCP hasn't completed yet, and we're queuing it for later. */
static void socket_notify_on_iocp(grpc_winsocket *socket,
                                  void(*cb)(void *, int), void *opaque,
                                  grpc_winsocket_callback_info *info) {
  int run_now = 0;
  GPR_ASSERT(!info->cb);
  gpr_mu_lock(&socket->state_mu);
  if (info->has_pending_iocp) {
    run_now = 1;
    info->has_pending_iocp = 0;
  } else {
    info->cb = cb;
    info->opaque = opaque;
  }
  gpr_mu_unlock(&socket->state_mu);
  if (run_now) cb(opaque, 1);
}

void grpc_socket_notify_on_write(grpc_winsocket *socket,
                                 void(*cb)(void *, int), void *opaque) {
  socket_notify_on_iocp(socket, cb, opaque, &socket->write_info);
}

void grpc_socket_notify_on_read(grpc_winsocket *socket,
                                void(*cb)(void *, int), void *opaque) {
  socket_notify_on_iocp(socket, cb, opaque, &socket->read_info);
}

#endif  /* GPR_WINSOCK_SOCKET */
