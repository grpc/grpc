/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <winsock2.h>

#include <limits>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/timer.h"

static ULONG g_iocp_kick_token;
static OVERLAPPED g_iocp_custom_overlap;

static gpr_atm g_custom_events = 0;

static HANDLE g_iocp;

static DWORD deadline_to_millis_timeout(grpc_millis deadline) {
  if (deadline == GRPC_MILLIS_INF_FUTURE) {
    return INFINITE;
  }
  grpc_millis now = grpc_core::ExecCtx::Get()->Now();
  if (deadline < now) return 0;
  grpc_millis timeout = deadline - now;
  if (timeout > std::numeric_limits<DWORD>::max()) return INFINITE;
  return static_cast<DWORD>(deadline - now);
}

grpc_iocp_work_status grpc_iocp_work(grpc_millis deadline) {
  BOOL success;
  DWORD bytes = 0;
  DWORD flags = 0;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;
  grpc_winsocket* socket;
  grpc_winsocket_callback_info* info;
  GRPC_STATS_INC_SYSCALL_POLL();
  success =
      GetQueuedCompletionStatus(g_iocp, &bytes, &completion_key, &overlapped,
                                deadline_to_millis_timeout(deadline));
  grpc_core::ExecCtx::Get()->InvalidateNow();
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

  socket = (grpc_winsocket*)completion_key;
  if (overlapped == &socket->write_info.overlapped) {
    info = &socket->write_info;
  } else if (overlapped == &socket->read_info.overlapped) {
    info = &socket->read_info;
  } else {
    abort();
  }
  if (socket->shutdown_called) {
    info->bytes_transferred = 0;
    info->wsa_error = WSA_OPERATION_ABORTED;
  } else {
    success = WSAGetOverlappedResult(socket->socket, &info->overlapped, &bytes,
                                     FALSE, &flags);
    info->bytes_transferred = bytes;
    info->wsa_error = success ? 0 : WSAGetLastError();
  }
  GPR_ASSERT(overlapped == &info->overlapped);
  grpc_socket_become_ready(socket, info);
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
  grpc_core::ExecCtx exec_ctx;
  grpc_iocp_work_status work_status;

  do {
    work_status = grpc_iocp_work(GRPC_MILLIS_INF_PAST);
  } while (work_status == GRPC_IOCP_WORK_KICK ||
           grpc_core::ExecCtx::Get()->Flush());
}

void grpc_iocp_shutdown(void) {
  grpc_core::ExecCtx exec_ctx;
  while (gpr_atm_acq_load(&g_custom_events)) {
    grpc_iocp_work(GRPC_MILLIS_INF_FUTURE);
    grpc_core::ExecCtx::Get()->Flush();
  }

  GPR_ASSERT(CloseHandle(g_iocp));
}

void grpc_iocp_add_socket(grpc_winsocket* socket) {
  HANDLE ret;
  if (socket->added_to_iocp) return;
  ret = CreateIoCompletionPort((HANDLE)socket->socket, g_iocp,
                               (uintptr_t)socket, 0);
  if (!ret) {
    char* utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "Unable to add socket to iocp: %s", utf8_message);
    gpr_free(utf8_message);
    __debugbreak();
    abort();
  }
  socket->added_to_iocp = 1;
  GPR_ASSERT(ret == g_iocp);
}

#endif /* GRPC_WINSOCK_SOCKET */
