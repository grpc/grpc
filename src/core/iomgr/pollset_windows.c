/*
 *
 * Copyright 2014, Google Inc.
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
#include <grpc/support/thd.h>

#include "src/core/iomgr/alarm_internal.h"
#include "src/core/iomgr/socket_windows.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/pollset_windows.h"

static grpc_pollset g_global_pollset;
static ULONG g_pollset_kick_token;
static OVERLAPPED g_pollset_custom_overlap;

static gpr_event g_shutdown_global_poller;
static gpr_event g_global_poller_done;

void grpc_pollset_init(grpc_pollset *pollset) {
  pollset->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL,
                                         (ULONG_PTR)NULL, 0);
  GPR_ASSERT(pollset->iocp);
}

void grpc_pollset_destroy(grpc_pollset *pollset) {
  BOOL status;
  status = CloseHandle(pollset->iocp);
  GPR_ASSERT(status);
}

static int pollset_poll(grpc_pollset *pollset,
                        gpr_timespec deadline, gpr_timespec now) {
  BOOL success;
  DWORD bytes = 0;
  DWORD flags = 0;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;
  gpr_timespec wait_time = gpr_time_sub(deadline, now);
  grpc_winsocket *socket;
  grpc_winsocket_callback_info *info;
  void(*f)(void *, int) = NULL;
  void *opaque = NULL;
  success = GetQueuedCompletionStatus(pollset->iocp, &bytes,
                                     &completion_key, &overlapped,
                                     gpr_time_to_millis(wait_time));

  if (!success && !overlapped) {
    /* The deadline got attained. */
    return 0;
  }
  GPR_ASSERT(completion_key && overlapped);
  if (overlapped == &g_pollset_custom_overlap) {
    if (completion_key == (ULONG_PTR) &g_pollset_kick_token) {
      /* We were awoken from a kick. */
      gpr_log(GPR_DEBUG, "pollset_poll - got a kick");
      return 1;
    }
    gpr_log(GPR_ERROR, "Unknown custom completion key.");
    abort();
  }

  socket = (grpc_winsocket*) completion_key;
  if (overlapped == &socket->write_info.overlapped) {
    gpr_log(GPR_DEBUG, "pollset_poll - got write packet");
    info = &socket->write_info;
  } else if (overlapped == &socket->read_info.overlapped) {
    gpr_log(GPR_DEBUG, "pollset_poll - got read packet");
    info = &socket->read_info;
  } else {
    gpr_log(GPR_ERROR, "Unknown IOCP operation");
    abort();
  }
  success = WSAGetOverlappedResult(socket->socket, &info->overlapped, &bytes,
                                   FALSE, &flags);
  gpr_log(GPR_DEBUG, "bytes: %u, flags: %u - op %s", bytes, flags,
          success ? "succeeded" : "failed");
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

  return 1;
}

int grpc_pollset_work(grpc_pollset *pollset, gpr_timespec deadline) {
  gpr_timespec now;
  now = gpr_now();
  if (gpr_time_cmp(now, deadline) > 0) {
    return 0;
  }
  if (grpc_maybe_call_delayed_callbacks(NULL, 1)) {
    return 1;
  }
  if (grpc_alarm_check(NULL, now, &deadline)) {
    return 1;
  }
  return pollset_poll(pollset, deadline, now);
}

void grpc_pollset_kick(grpc_pollset *pollset) {
  BOOL status;
  status = PostQueuedCompletionStatus(pollset->iocp, 0,
                                      (ULONG_PTR) &g_pollset_kick_token,
                                      &g_pollset_custom_overlap);
  GPR_ASSERT(status);
}

static void global_poller(void *p) {
  while (!gpr_event_get(&g_shutdown_global_poller)) {
    grpc_pollset_work(&g_global_pollset, gpr_inf_future);
  }

  gpr_event_set(&g_global_poller_done, (void *) 1);
}

void grpc_pollset_global_init(void) {
  gpr_thd_id id;

  grpc_pollset_init(&g_global_pollset);
  gpr_event_init(&g_global_poller_done);
  gpr_event_init(&g_shutdown_global_poller);
  gpr_thd_new(&id, global_poller, NULL, NULL);
}

void grpc_pollset_global_shutdown(void) {
  gpr_event_set(&g_shutdown_global_poller, (void *) 1);
  grpc_pollset_kick(&g_global_pollset);
  gpr_event_wait(&g_global_poller_done, gpr_inf_future);
  grpc_pollset_destroy(&g_global_pollset);
}

void grpc_pollset_add_handle(grpc_pollset *pollset, grpc_winsocket *socket) {
  HANDLE ret = CreateIoCompletionPort((HANDLE) socket->socket, pollset->iocp,
                                      (gpr_uintptr) socket, 0);
  GPR_ASSERT(ret == pollset->iocp);
}

static void handle_notify_on_iocp(grpc_winsocket *socket,
                                  void(*cb)(void *, int), void *opaque,
                                  grpc_winsocket_callback_info *info) {
  int run_now = 0;
  GPR_ASSERT(!info->cb);
  gpr_mu_lock(&socket->state_mu);
  if (info->has_pending_iocp) {
    run_now = 1;
    info->has_pending_iocp = 0;
    gpr_log(GPR_DEBUG, "handle_notify_on_iocp - runs now");
  } else {
    info->cb = cb;
    info->opaque = opaque;
    gpr_log(GPR_DEBUG, "handle_notify_on_iocp - queued");
  }
  gpr_mu_unlock(&socket->state_mu);
  if (run_now) cb(opaque, 1);
}

void grpc_handle_notify_on_write(grpc_winsocket *socket,
                                 void(*cb)(void *, int), void *opaque) {
  gpr_log(GPR_DEBUG, "grpc_handle_notify_on_write");
  handle_notify_on_iocp(socket, cb, opaque, &socket->write_info);
}

void grpc_handle_notify_on_read(grpc_winsocket *socket,
                                void(*cb)(void *, int), void *opaque) {
  gpr_log(GPR_DEBUG, "grpc_handle_notify_on_read");
  handle_notify_on_iocp(socket, cb, opaque, &socket->read_info);
}

grpc_pollset *grpc_global_pollset(void) {
  return &g_global_pollset;
}

#endif  /* GPR_WINSOCK_SOCKET */
