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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <winsock2.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/timer.h"

static ULONG g_iocp_kick_token;
static OVERLAPPED g_iocp_custom_overlap;

static gpr_atm g_custom_events = 0;

static HANDLE g_iocp;

static DWORD deadline_to_millis_timeout(gpr_timespec deadline,
                                        gpr_timespec now) {
  gpr_timespec timeout;
  static const int64_t max_spin_polling_us = 10;
  if (gpr_time_cmp(deadline, gpr_inf_future(deadline.clock_type)) == 0) {
    return INFINITE;
  }
  if (gpr_time_cmp(deadline, gpr_time_add(now, gpr_time_from_micros(
                                                   max_spin_polling_us,
                                                   GPR_TIMESPAN))) <= 0) {
    return 0;
  }
  timeout = gpr_time_sub(deadline, now);
  return (DWORD)gpr_time_to_millis(gpr_time_add(
      timeout, gpr_time_from_nanos(GPR_NS_PER_MS - 1, GPR_TIMESPAN)));
}

grpc_iocp_work_status grpc_iocp_work(grpc_exec_ctx *exec_ctx,
                                     gpr_timespec deadline) {
  BOOL success;
  DWORD bytes = 0;
  DWORD flags = 0;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;
  grpc_winsocket *socket;
  grpc_winsocket_callback_info *info;
  success = GetQueuedCompletionStatus(
      g_iocp, &bytes, &completion_key, &overlapped,
      deadline_to_millis_timeout(deadline, gpr_now(deadline.clock_type)));
  if (success == 0 && overlapped == NULL) {
    return GRPC_IOCP_WORK_TIMEOUT;
  }
  GPR_ASSERT(completion_key && overlapped);
  if (overlapped == &g_iocp_custom_overlap) {
    gpr_atm_full_fetch_add(&g_custom_events, -1);
    if (completion_key == (ULONG_PTR)&g_iocp_kick_token) {
      /* We were awoken from a kick. */
      return GRPC_IOCP_WORK_KICK;
    }
    gpr_log(GPR_ERROR, "Unknown custom completion key.");
    abort();
  }

  socket = (grpc_winsocket *)completion_key;
  if (overlapped == &socket->write_info.overlapped) {
    info = &socket->write_info;
  } else if (overlapped == &socket->read_info.overlapped) {
    info = &socket->read_info;
  } else {
    abort();
  }
  success = WSAGetOverlappedResult(socket->socket, &info->overlapped, &bytes,
                                   FALSE, &flags);
  info->bytes_transfered = bytes;
  info->wsa_error = success ? 0 : WSAGetLastError();
  GPR_ASSERT(overlapped == &info->overlapped);
  grpc_socket_become_ready(exec_ctx, socket, info);
  return GRPC_IOCP_WORK_WORK;
}

void grpc_iocp_init(void) {
  g_iocp =
      CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
  GPR_ASSERT(g_iocp);
}

void grpc_iocp_kick(void) {
  BOOL success;

  gpr_atm_full_fetch_add(&g_custom_events, 1);
  success = PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)&g_iocp_kick_token,
                                       &g_iocp_custom_overlap);
  GPR_ASSERT(success);
}

void grpc_iocp_flush(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_iocp_work_status work_status;

  do {
    work_status = grpc_iocp_work(&exec_ctx, gpr_inf_past(GPR_CLOCK_MONOTONIC));
  } while (work_status == GRPC_IOCP_WORK_KICK ||
           grpc_exec_ctx_flush(&exec_ctx));
}

void grpc_iocp_shutdown(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (gpr_atm_acq_load(&g_custom_events)) {
    grpc_iocp_work(&exec_ctx, gpr_inf_future(GPR_CLOCK_MONOTONIC));
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(CloseHandle(g_iocp));
}

void grpc_iocp_add_socket(grpc_winsocket *socket) {
  HANDLE ret;
  if (socket->added_to_iocp) return;
  ret = CreateIoCompletionPort((HANDLE)socket->socket, g_iocp,
                               (uintptr_t)socket, 0);
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

#endif /* GRPC_WINSOCK_SOCKET */
