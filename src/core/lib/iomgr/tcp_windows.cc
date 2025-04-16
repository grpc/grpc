//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <limits.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/util/crash.h"
#include "src/core/util/string.h"
#include "src/core/util/useful.h"

#if defined(__MSYS__) && defined(GPR_ARCH_64)
// Nasty workaround for nasty bug when using the 64 bits msys compiler
// in conjunction with Microsoft Windows headers.
#define GRPC_FIONBIO _IOW('f', 126, uint32_t)
#else
#define GRPC_FIONBIO FIONBIO
#endif

grpc_error_handle grpc_tcp_set_non_block(SOCKET sock) {
  int status;
  uint32_t param = 1;
  DWORD ret;
  status = WSAIoctl(sock, GRPC_FIONBIO, &param, sizeof(param), NULL, 0, &ret,
                    NULL, NULL);
  return status == 0
             ? absl::OkStatus()
             : GRPC_WSA_ERROR(WSAGetLastError(), "WSAIoctl(GRPC_FIONBIO)");
}

static grpc_error_handle set_dualstack(SOCKET sock) {
  int status;
  unsigned long param = 0;
  status = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&param,
                      sizeof(param));
  return status == 0
             ? absl::OkStatus()
             : GRPC_WSA_ERROR(WSAGetLastError(), "setsockopt(IPV6_V6ONLY)");
}

static grpc_error_handle enable_socket_low_latency(SOCKET sock) {
  int status;
  BOOL param = TRUE;
  status = ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                        reinterpret_cast<char*>(&param), sizeof(param));
  if (status == SOCKET_ERROR) {
    status = WSAGetLastError();
  }
  return status == 0 ? absl::OkStatus()
                     : GRPC_WSA_ERROR(status, "setsockopt(TCP_NODELAY)");
}

grpc_error_handle grpc_tcp_prepare_socket(SOCKET sock) {
  grpc_error_handle err;
  err = grpc_tcp_set_non_block(sock);
  if (!err.ok()) return err;
  err = set_dualstack(sock);
  if (!err.ok()) return err;
  err = enable_socket_low_latency(sock);
  if (!err.ok()) return err;
  return absl::OkStatus();
}

typedef struct grpc_tcp {
  // This is our C++ class derivation emulation.
  grpc_endpoint base;
  // The one socket this endpoint is using.
  grpc_winsocket* socket;
  // Refcounting how many operations are in progress.
  gpr_refcount refcount;

  grpc_closure on_read;
  grpc_closure on_write;

  grpc_closure* read_cb;
  grpc_closure* write_cb;

  // garbage after the last read
  grpc_slice_buffer last_read_buffer;

  grpc_slice_buffer* write_slices;
  grpc_slice_buffer* read_slices;

  // The IO Completion Port runs from another thread. We need some mechanism
  // to protect ourselves when requesting a shutdown.
  gpr_mu mu;
  int shutting_down;

  std::string peer_string;
  std::string local_address;
} grpc_tcp;

static void tcp_free(grpc_tcp* tcp) {
  grpc_winsocket_destroy(tcp->socket);
  gpr_mu_destroy(&tcp->mu);
  grpc_slice_buffer_destroy(&tcp->last_read_buffer);
  delete tcp;
}

#ifndef NDEBUG
#define TCP_UNREF(tcp, reason) tcp_unref((tcp), (reason), __FILE__, __LINE__)
#define TCP_REF(tcp, reason) tcp_ref((tcp), (reason), __FILE__, __LINE__)
static void tcp_unref(grpc_tcp* tcp, const char* reason, const char* file,
                      int line) {
  if (GRPC_TRACE_FLAG_ENABLED(tcp)) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    VLOG(2).AtLocation(file, line) << "TCP unref " << tcp << " : " << reason
                                   << " " << val << " -> " << val - 1;
  }
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp* tcp, const char* reason, const char* file,
                    int line) {
  if (GRPC_TRACE_FLAG_ENABLED(tcp)) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    VLOG(2).AtLocation(file, line) << "TCP   ref " << tcp << " : " << reason
                                   << " " << val << " -> " << val + 1;
  }
  gpr_ref(&tcp->refcount);
}
#else
#define TCP_UNREF(tcp, reason) tcp_unref((tcp))
#define TCP_REF(tcp, reason) tcp_ref((tcp))
static void tcp_unref(grpc_tcp* tcp) {
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp* tcp) { gpr_ref(&tcp->refcount); }
#endif

// Asynchronous callback from the IOCP, or the background thread.
static void on_read(void* tcpp, grpc_error_handle error) {
  grpc_tcp* tcp = (grpc_tcp*)tcpp;
  grpc_closure* cb = tcp->read_cb;
  grpc_winsocket* socket = tcp->socket;
  grpc_winsocket_callback_info* info = &socket->read_info;

  GRPC_TRACE_LOG(tcp, INFO) << "TCP:" << tcp << " on_read";

  if (error.ok()) {
    if (info->wsa_error != 0 && !tcp->shutting_down) {
      error = GRPC_WSA_ERROR(info->wsa_error, "IOCP/Socket");
      grpc_slice_buffer_reset_and_unref(tcp->read_slices);
    } else {
      if (info->bytes_transferred != 0 && !tcp->shutting_down) {
        CHECK((size_t)info->bytes_transferred <= tcp->read_slices->length);
        if (static_cast<size_t>(info->bytes_transferred) !=
            tcp->read_slices->length) {
          grpc_slice_buffer_trim_end(
              tcp->read_slices,
              tcp->read_slices->length -
                  static_cast<size_t>(info->bytes_transferred),
              &tcp->last_read_buffer);
        }
        CHECK((size_t)info->bytes_transferred == tcp->read_slices->length);

        if (GRPC_TRACE_FLAG_ENABLED(tcp) && ABSL_VLOG_IS_ON(2)) {
          size_t i;
          for (i = 0; i < tcp->read_slices->count; i++) {
            char* dump = grpc_dump_slice(tcp->read_slices->slices[i],
                                         GPR_DUMP_HEX | GPR_DUMP_ASCII);
            VLOG(2) << "READ " << tcp << " (peer=" << tcp->peer_string
                    << "): " << dump;
            gpr_free(dump);
          }
        }
      } else {
        GRPC_TRACE_LOG(tcp, INFO) << "TCP:" << tcp << " unref read_slice";
        grpc_slice_buffer_reset_and_unref(tcp->read_slices);
        error = grpc_error_set_int(
            tcp->shutting_down ? GRPC_ERROR_CREATE("TCP stream shutting down")
                               : GRPC_ERROR_CREATE("End of TCP stream"),
            grpc_core::StatusIntProperty::kRpcStatus, GRPC_STATUS_UNAVAILABLE);
      }
    }
  }

  tcp->read_cb = NULL;
  TCP_UNREF(tcp, "read");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}

#define DEFAULT_TARGET_READ_SIZE 8192
#define MAX_WSABUF_COUNT 16
static void win_read(grpc_endpoint* ep, grpc_slice_buffer* read_slices,
                     grpc_closure* cb, bool /* urgent */,
                     int /* min_progress_size */) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  grpc_winsocket* handle = tcp->socket;
  grpc_winsocket_callback_info* info = &handle->read_info;
  int status;
  DWORD bytes_read = 0;
  DWORD flags = 0;
  WSABUF buffers[MAX_WSABUF_COUNT];
  size_t i;

  GRPC_TRACE_LOG(tcp, INFO) << "TCP:" << tcp << " win_read";

  if (tcp->shutting_down) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        grpc_error_set_int(GRPC_ERROR_CREATE("TCP socket is shutting down"),
                           grpc_core::StatusIntProperty::kRpcStatus,
                           GRPC_STATUS_UNAVAILABLE));
    return;
  }

  tcp->read_cb = cb;
  tcp->read_slices = read_slices;
  grpc_slice_buffer_reset_and_unref(read_slices);
  grpc_slice_buffer_swap(read_slices, &tcp->last_read_buffer);

  if (tcp->read_slices->length < DEFAULT_TARGET_READ_SIZE / 2 &&
      tcp->read_slices->count < MAX_WSABUF_COUNT) {
    // TODO(jtattermusch): slice should be allocated using resource quota
    grpc_slice_buffer_add(tcp->read_slices,
                          GRPC_SLICE_MALLOC(DEFAULT_TARGET_READ_SIZE));
  }

  CHECK(tcp->read_slices->count <= MAX_WSABUF_COUNT);
  for (i = 0; i < tcp->read_slices->count; i++) {
    buffers[i].len = (ULONG)GRPC_SLICE_LENGTH(
        tcp->read_slices->slices[i]);  // we know slice size fits in 32bit.
    buffers[i].buf = (char*)GRPC_SLICE_START_PTR(tcp->read_slices->slices[i]);
  }

  TCP_REF(tcp, "read");

  // First let's try a synchronous, non-blocking read.
  status = WSARecv(tcp->socket->socket, buffers, (DWORD)tcp->read_slices->count,
                   &bytes_read, &flags, NULL, NULL);
  info->wsa_error = status == 0 ? 0 : WSAGetLastError();

  // Did we get data immediately ? Yay.
  if (info->wsa_error != WSAEWOULDBLOCK) {
    info->bytes_transferred = bytes_read;
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &tcp->on_read, absl::OkStatus());
    return;
  }

  // Otherwise, let's retry, by queuing a read.
  memset(&tcp->socket->read_info.overlapped, 0, sizeof(OVERLAPPED));
  status = WSARecv(tcp->socket->socket, buffers, (DWORD)tcp->read_slices->count,
                   &bytes_read, &flags, &info->overlapped, NULL);

  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      info->wsa_error = wsa_error;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, &tcp->on_read,
                              GRPC_WSA_ERROR(info->wsa_error, "WSARecv"));
      return;
    }
  }

  grpc_socket_notify_on_read(tcp->socket, &tcp->on_read);
}

// Asynchronous callback from the IOCP, or the background thread.
static void on_write(void* tcpp, grpc_error_handle error) {
  grpc_tcp* tcp = (grpc_tcp*)tcpp;
  grpc_winsocket* handle = tcp->socket;
  grpc_winsocket_callback_info* info = &handle->write_info;
  grpc_closure* cb;

  GRPC_TRACE_LOG(tcp, INFO) << "TCP:" << tcp << " on_write";

  gpr_mu_lock(&tcp->mu);
  cb = tcp->write_cb;
  tcp->write_cb = NULL;
  gpr_mu_unlock(&tcp->mu);

  if (error.ok()) {
    if (info->wsa_error != 0) {
      error = GRPC_WSA_ERROR(info->wsa_error, "WSASend");
    } else {
      CHECK(info->bytes_transferred <= tcp->write_slices->length);
    }
  }

  TCP_UNREF(tcp, "write");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
}

// Initiates a write.
static void win_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                      grpc_closure* cb, void* /* arg */,
                      int /* max_frame_size */) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  grpc_winsocket* socket = tcp->socket;
  grpc_winsocket_callback_info* info = &socket->write_info;
  unsigned i;
  DWORD bytes_sent;
  int status;
  WSABUF local_buffers[MAX_WSABUF_COUNT];
  WSABUF* allocated = NULL;
  WSABUF* buffers = local_buffers;
  size_t len, async_buffers_offset = 0;

  if (GRPC_TRACE_FLAG_ENABLED(tcp) && ABSL_VLOG_IS_ON(2)) {
    size_t i;
    for (i = 0; i < slices->count; i++) {
      char* data =
          grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      VLOG(2) << "WRITE " << tcp << " (peer=" << tcp->peer_string
              << "): " << data;
      gpr_free(data);
    }
  }

  if (tcp->shutting_down) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        grpc_error_set_int(GRPC_ERROR_CREATE("TCP socket is shutting down"),
                           grpc_core::StatusIntProperty::kRpcStatus,
                           GRPC_STATUS_UNAVAILABLE));
    return;
  }

  tcp->write_cb = cb;
  tcp->write_slices = slices;
  CHECK(tcp->write_slices->count <= UINT_MAX);
  if (tcp->write_slices->count > GPR_ARRAY_SIZE(local_buffers)) {
    buffers = (WSABUF*)gpr_malloc(sizeof(WSABUF) * tcp->write_slices->count);
    allocated = buffers;
  }

  for (i = 0; i < tcp->write_slices->count; i++) {
    len = GRPC_SLICE_LENGTH(tcp->write_slices->slices[i]);
    CHECK(len <= ULONG_MAX);
    buffers[i].len = (ULONG)len;
    buffers[i].buf = (char*)GRPC_SLICE_START_PTR(tcp->write_slices->slices[i]);
  }

  // First, let's try a synchronous, non-blocking write.
  status = WSASend(socket->socket, buffers, (DWORD)tcp->write_slices->count,
                   &bytes_sent, 0, NULL, NULL);

  if (status == 0) {
    if (bytes_sent == tcp->write_slices->length) {
      info->wsa_error = 0;
      grpc_error_handle error = absl::OkStatus();
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
      if (allocated) gpr_free(allocated);
      return;
    }

    // The data was not completely delivered, we should send the rest of
    // them by doing an async write operation.
    for (i = 0; i < tcp->write_slices->count; i++) {
      if (buffers[i].len > bytes_sent) {
        buffers[i].buf += bytes_sent;
        buffers[i].len -= bytes_sent;
        break;
      }
      bytes_sent -= buffers[i].len;
      async_buffers_offset++;
    }
  } else {
    info->wsa_error = WSAGetLastError();

    // We would kind of expect to get a WSAEWOULDBLOCK here, especially on a
    // busy connection that has its send queue filled up. But if we don't, then
    // we can avoid doing an async write operation at all.
    if (info->wsa_error != WSAEWOULDBLOCK) {
      grpc_error_handle error = GRPC_WSA_ERROR(info->wsa_error, "WSASend");
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
      if (allocated) gpr_free(allocated);
      return;
    }
  }

  TCP_REF(tcp, "write");

  // If we got a WSAEWOULDBLOCK earlier, then we need to re-do the same
  // operation, this time asynchronously.
  memset(&socket->write_info.overlapped, 0, sizeof(OVERLAPPED));
  status = WSASend(socket->socket, buffers + async_buffers_offset,
                   (DWORD)(tcp->write_slices->count - async_buffers_offset),
                   NULL, 0, &socket->write_info.overlapped, NULL);
  if (allocated) gpr_free(allocated);

  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      TCP_UNREF(tcp, "write");
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,
                              GRPC_WSA_ERROR(wsa_error, "WSASend"));
      return;
    }
  }

  // As all is now setup, we can now ask for the IOCP notification. It may
  // trigger the callback immediately however, but no matter.
  grpc_socket_notify_on_write(socket, &tcp->on_write);
}

static void win_add_to_pollset(grpc_endpoint* ep, grpc_pollset* ps) {
  grpc_tcp* tcp;
  (void)ps;
  tcp = (grpc_tcp*)ep;
  grpc_iocp_add_socket(tcp->socket);
}

static void win_add_to_pollset_set(grpc_endpoint* ep, grpc_pollset_set* pss) {
  grpc_tcp* tcp;
  (void)pss;
  tcp = (grpc_tcp*)ep;
  grpc_iocp_add_socket(tcp->socket);
}

static void win_delete_from_pollset_set(grpc_endpoint* /* ep */,
                                        grpc_pollset_set* /* pss */) {}

// Initiates a shutdown of the TCP endpoint. This will queue abort callbacks
// for the potential read and write operations. It is up to the caller to
// guarantee this isn't called in parallel to a read or write request, so
// we're not going to protect against these. However the IO Completion Port
// callback will happen from another thread, so we need to protect against
// concurrent access of the data structure in that regard.
static void win_destroy(grpc_endpoint* ep) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  gpr_mu_lock(&tcp->mu);
  // At that point, what may happen is that we're already inside the IOCP
  // callback. See the comments in on_read and on_write.
  tcp->shutting_down = 1;
  grpc_winsocket_shutdown(tcp->socket);
  gpr_mu_unlock(&tcp->mu);
  grpc_slice_buffer_reset_and_unref(&tcp->last_read_buffer);
  TCP_UNREF(tcp, "destroy");
}

static absl::string_view win_get_peer(grpc_endpoint* ep) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  return tcp->peer_string;
}

static absl::string_view win_get_local_address(grpc_endpoint* ep) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  return tcp->local_address;
}

static int win_get_fd(grpc_endpoint* /* ep */) { return -1; }

static bool win_can_track_err(grpc_endpoint* /* ep */) { return false; }

static grpc_endpoint_vtable vtable = {win_read,
                                      win_write,
                                      win_add_to_pollset,
                                      win_add_to_pollset_set,
                                      win_delete_from_pollset_set,
                                      win_destroy,
                                      win_get_peer,
                                      win_get_local_address,
                                      win_get_fd,
                                      win_can_track_err};

grpc_endpoint* grpc_tcp_create(grpc_winsocket* socket,
                               absl::string_view peer_string) {
  // TODO(jtattermusch): C++ize grpc_tcp and its dependencies (i.e. add
  // constructors) to ensure proper initialization
  grpc_tcp* tcp = new grpc_tcp{};
  tcp->base.vtable = &vtable;
  tcp->socket = socket;
  gpr_mu_init(&tcp->mu);
  gpr_ref_init(&tcp->refcount, 1);
  GRPC_CLOSURE_INIT(&tcp->on_read, on_read, tcp, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&tcp->on_write, on_write, tcp, grpc_schedule_on_exec_ctx);
  grpc_resolved_address resolved_local_addr;
  resolved_local_addr.len = sizeof(resolved_local_addr.addr);
  absl::StatusOr<std::string> addr_uri;
  if (getsockname(tcp->socket->socket,
                  reinterpret_cast<sockaddr*>(resolved_local_addr.addr),
                  &resolved_local_addr.len) < 0 ||
      !(addr_uri = grpc_sockaddr_to_uri(&resolved_local_addr)).ok()) {
    tcp->local_address = "";
  } else {
    tcp->local_address = grpc_sockaddr_to_uri(&resolved_local_addr).value();
  }
  tcp->peer_string = std::string(peer_string);
  grpc_slice_buffer_init(&tcp->last_read_buffer);
  return &tcp->base;
}

#endif  // GRPC_WINSOCK_SOCKET
