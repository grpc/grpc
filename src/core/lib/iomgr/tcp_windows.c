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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <limits.h>

#include "src/core/lib/iomgr/network_status_tracker.h"
#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"

#if defined(__MSYS__) && defined(GPR_ARCH_64)
/* Nasty workaround for nasty bug when using the 64 bits msys compiler
   in conjunction with Microsoft Windows headers. */
#define GRPC_FIONBIO _IOW('f', 126, uint32_t)
#else
#define GRPC_FIONBIO FIONBIO
#endif

grpc_tracer_flag grpc_tcp_trace = GRPC_TRACER_INITIALIZER(false, "tcp");

static grpc_error *set_non_block(SOCKET sock) {
  int status;
  uint32_t param = 1;
  DWORD ret;
  status = WSAIoctl(sock, GRPC_FIONBIO, &param, sizeof(param), NULL, 0, &ret,
                    NULL, NULL);
  return status == 0
             ? GRPC_ERROR_NONE
             : GRPC_WSA_ERROR(WSAGetLastError(), "WSAIoctl(GRPC_FIONBIO)");
}

static grpc_error *set_dualstack(SOCKET sock) {
  int status;
  unsigned long param = 0;
  status = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&param,
                      sizeof(param));
  return status == 0
             ? GRPC_ERROR_NONE
             : GRPC_WSA_ERROR(WSAGetLastError(), "setsockopt(IPV6_V6ONLY)");
}

grpc_error *grpc_tcp_prepare_socket(SOCKET sock) {
  grpc_error *err;
  err = set_non_block(sock);
  if (err != GRPC_ERROR_NONE) return err;
  err = set_dualstack(sock);
  if (err != GRPC_ERROR_NONE) return err;
  return GRPC_ERROR_NONE;
}

typedef struct grpc_tcp {
  /* This is our C++ class derivation emulation. */
  grpc_endpoint base;
  /* The one socket this endpoint is using. */
  grpc_winsocket *socket;
  /* Refcounting how many operations are in progress. */
  gpr_refcount refcount;

  grpc_closure on_read;
  grpc_closure on_write;

  grpc_closure *read_cb;
  grpc_closure *write_cb;
  grpc_slice read_slice;
  grpc_slice_buffer *write_slices;
  grpc_slice_buffer *read_slices;

  grpc_resource_user *resource_user;

  /* The IO Completion Port runs from another thread. We need some mechanism
     to protect ourselves when requesting a shutdown. */
  gpr_mu mu;
  int shutting_down;
  grpc_error *shutdown_error;

  char *peer_string;
} grpc_tcp;

static void tcp_free(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp) {
  grpc_winsocket_destroy(tcp->socket);
  gpr_mu_destroy(&tcp->mu);
  gpr_free(tcp->peer_string);
  grpc_resource_user_unref(exec_ctx, tcp->resource_user);
  if (tcp->shutting_down) GRPC_ERROR_UNREF(tcp->shutdown_error);
  gpr_free(tcp);
}

#ifndef NDEBUG
#define TCP_UNREF(exec_ctx, tcp, reason) \
  tcp_unref((exec_ctx), (tcp), (reason), __FILE__, __LINE__)
#define TCP_REF(tcp, reason) tcp_ref((tcp), (reason), __FILE__, __LINE__)
static void tcp_unref(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp,
                      const char *reason, const char *file, int line) {
  if (GRPC_TRACER_ON(grpc_tcp_trace)) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "TCP unref %p : %s %" PRIdPTR " -> %" PRIdPTR, tcp, reason, val,
            val - 1);
  }
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(exec_ctx, tcp);
  }
}

static void tcp_ref(grpc_tcp *tcp, const char *reason, const char *file,
                    int line) {
  if (GRPC_TRACER_ON(grpc_tcp_trace)) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "TCP   ref %p : %s %" PRIdPTR " -> %" PRIdPTR, tcp, reason, val,
            val + 1);
  }
  gpr_ref(&tcp->refcount);
}
#else
#define TCP_UNREF(exec_ctx, tcp, reason) tcp_unref((exec_ctx), (tcp))
#define TCP_REF(tcp, reason) tcp_ref((tcp))
static void tcp_unref(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp) {
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(exec_ctx, tcp);
  }
}

static void tcp_ref(grpc_tcp *tcp) { gpr_ref(&tcp->refcount); }
#endif

/* Asynchronous callback from the IOCP, or the background thread. */
static void on_read(grpc_exec_ctx *exec_ctx, void *tcpp, grpc_error *error) {
  grpc_tcp *tcp = tcpp;
  grpc_closure *cb = tcp->read_cb;
  grpc_winsocket *socket = tcp->socket;
  grpc_slice sub;
  grpc_winsocket_callback_info *info = &socket->read_info;

  GRPC_ERROR_REF(error);

  if (error == GRPC_ERROR_NONE) {
    if (info->wsa_error != 0 && !tcp->shutting_down) {
      char *utf8_message = gpr_format_message(info->wsa_error);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(utf8_message);
      gpr_free(utf8_message);
      grpc_slice_unref_internal(exec_ctx, tcp->read_slice);
    } else {
      if (info->bytes_transfered != 0 && !tcp->shutting_down) {
        sub = grpc_slice_sub_no_ref(tcp->read_slice, 0, info->bytes_transfered);
        grpc_slice_buffer_add(tcp->read_slices, sub);
      } else {
        grpc_slice_unref_internal(exec_ctx, tcp->read_slice);
        error = tcp->shutting_down
                    ? GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                          "TCP stream shutting down", &tcp->shutdown_error, 1)
                    : GRPC_ERROR_CREATE_FROM_STATIC_STRING("End of TCP stream");
      }
    }
  }

  tcp->read_cb = NULL;
  TCP_UNREF(exec_ctx, tcp, "read");
  GRPC_CLOSURE_SCHED(exec_ctx, cb, error);
}

static void win_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                     grpc_slice_buffer *read_slices, grpc_closure *cb) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_winsocket *handle = tcp->socket;
  grpc_winsocket_callback_info *info = &handle->read_info;
  int status;
  DWORD bytes_read = 0;
  DWORD flags = 0;
  WSABUF buffer;

  if (tcp->shutting_down) {
    GRPC_CLOSURE_SCHED(
        exec_ctx, cb,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "TCP socket is shutting down", &tcp->shutdown_error, 1));
    return;
  }

  tcp->read_cb = cb;
  tcp->read_slices = read_slices;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, read_slices);

  tcp->read_slice = GRPC_SLICE_MALLOC(8192);

  buffer.len = (ULONG)GRPC_SLICE_LENGTH(
      tcp->read_slice);  // we know slice size fits in 32bit.
  buffer.buf = (char *)GRPC_SLICE_START_PTR(tcp->read_slice);

  TCP_REF(tcp, "read");

  /* First let's try a synchronous, non-blocking read. */
  status =
      WSARecv(tcp->socket->socket, &buffer, 1, &bytes_read, &flags, NULL, NULL);
  info->wsa_error = status == 0 ? 0 : WSAGetLastError();

  /* Did we get data immediately ? Yay. */
  if (info->wsa_error != WSAEWOULDBLOCK) {
    info->bytes_transfered = bytes_read;
    GRPC_CLOSURE_SCHED(exec_ctx, &tcp->on_read, GRPC_ERROR_NONE);
    return;
  }

  /* Otherwise, let's retry, by queuing a read. */
  memset(&tcp->socket->read_info.overlapped, 0, sizeof(OVERLAPPED));
  status = WSARecv(tcp->socket->socket, &buffer, 1, &bytes_read, &flags,
                   &info->overlapped, NULL);

  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      info->wsa_error = wsa_error;
      GRPC_CLOSURE_SCHED(exec_ctx, &tcp->on_read,
                         GRPC_WSA_ERROR(info->wsa_error, "WSARecv"));
      return;
    }
  }

  grpc_socket_notify_on_read(exec_ctx, tcp->socket, &tcp->on_read);
}

/* Asynchronous callback from the IOCP, or the background thread. */
static void on_write(grpc_exec_ctx *exec_ctx, void *tcpp, grpc_error *error) {
  grpc_tcp *tcp = (grpc_tcp *)tcpp;
  grpc_winsocket *handle = tcp->socket;
  grpc_winsocket_callback_info *info = &handle->write_info;
  grpc_closure *cb;

  GRPC_ERROR_REF(error);

  gpr_mu_lock(&tcp->mu);
  cb = tcp->write_cb;
  tcp->write_cb = NULL;
  gpr_mu_unlock(&tcp->mu);

  if (error == GRPC_ERROR_NONE) {
    if (info->wsa_error != 0) {
      error = GRPC_WSA_ERROR(info->wsa_error, "WSASend");
    } else {
      GPR_ASSERT(info->bytes_transfered == tcp->write_slices->length);
    }
  }

  TCP_UNREF(exec_ctx, tcp, "write");
  GRPC_CLOSURE_SCHED(exec_ctx, cb, error);
}

/* Initiates a write. */
static void win_write(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                      grpc_slice_buffer *slices, grpc_closure *cb) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_winsocket *socket = tcp->socket;
  grpc_winsocket_callback_info *info = &socket->write_info;
  unsigned i;
  DWORD bytes_sent;
  int status;
  WSABUF local_buffers[16];
  WSABUF *allocated = NULL;
  WSABUF *buffers = local_buffers;
  size_t len;

  if (tcp->shutting_down) {
    GRPC_CLOSURE_SCHED(
        exec_ctx, cb,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "TCP socket is shutting down", &tcp->shutdown_error, 1));
    return;
  }

  tcp->write_cb = cb;
  tcp->write_slices = slices;
  GPR_ASSERT(tcp->write_slices->count <= UINT_MAX);
  if (tcp->write_slices->count > GPR_ARRAY_SIZE(local_buffers)) {
    buffers = (WSABUF *)gpr_malloc(sizeof(WSABUF) * tcp->write_slices->count);
    allocated = buffers;
  }

  for (i = 0; i < tcp->write_slices->count; i++) {
    len = GRPC_SLICE_LENGTH(tcp->write_slices->slices[i]);
    GPR_ASSERT(len <= ULONG_MAX);
    buffers[i].len = (ULONG)len;
    buffers[i].buf = (char *)GRPC_SLICE_START_PTR(tcp->write_slices->slices[i]);
  }

  /* First, let's try a synchronous, non-blocking write. */
  status = WSASend(socket->socket, buffers, (DWORD)tcp->write_slices->count,
                   &bytes_sent, 0, NULL, NULL);
  info->wsa_error = status == 0 ? 0 : WSAGetLastError();

  /* We would kind of expect to get a WSAEWOULDBLOCK here, especially on a busy
     connection that has its send queue filled up. But if we don't, then we can
     avoid doing an async write operation at all. */
  if (info->wsa_error != WSAEWOULDBLOCK) {
    grpc_error *error = status == 0
                            ? GRPC_ERROR_NONE
                            : GRPC_WSA_ERROR(info->wsa_error, "WSASend");
    GRPC_CLOSURE_SCHED(exec_ctx, cb, error);
    if (allocated) gpr_free(allocated);
    return;
  }

  TCP_REF(tcp, "write");

  /* If we got a WSAEWOULDBLOCK earlier, then we need to re-do the same
     operation, this time asynchronously. */
  memset(&socket->write_info.overlapped, 0, sizeof(OVERLAPPED));
  status = WSASend(socket->socket, buffers, (DWORD)tcp->write_slices->count,
                   &bytes_sent, 0, &socket->write_info.overlapped, NULL);
  if (allocated) gpr_free(allocated);

  if (status != 0) {
    int wsa_error = WSAGetLastError();
    if (wsa_error != WSA_IO_PENDING) {
      TCP_UNREF(exec_ctx, tcp, "write");
      GRPC_CLOSURE_SCHED(exec_ctx, cb, GRPC_WSA_ERROR(wsa_error, "WSASend"));
      return;
    }
  }

  /* As all is now setup, we can now ask for the IOCP notification. It may
     trigger the callback immediately however, but no matter. */
  grpc_socket_notify_on_write(exec_ctx, socket, &tcp->on_write);
}

static void win_add_to_pollset(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                               grpc_pollset *ps) {
  grpc_tcp *tcp;
  (void)ps;
  tcp = (grpc_tcp *)ep;
  grpc_iocp_add_socket(tcp->socket);
}

static void win_add_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                   grpc_pollset_set *pss) {
  grpc_tcp *tcp;
  (void)pss;
  tcp = (grpc_tcp *)ep;
  grpc_iocp_add_socket(tcp->socket);
}

/* Initiates a shutdown of the TCP endpoint. This will queue abort callbacks
   for the potential read and write operations. It is up to the caller to
   guarantee this isn't called in parallel to a read or write request, so
   we're not going to protect against these. However the IO Completion Port
   callback will happen from another thread, so we need to protect against
   concurrent access of the data structure in that regard. */
static void win_shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                         grpc_error *why) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  gpr_mu_lock(&tcp->mu);
  /* At that point, what may happen is that we're already inside the IOCP
     callback. See the comments in on_read and on_write. */
  if (!tcp->shutting_down) {
    tcp->shutting_down = 1;
    tcp->shutdown_error = why;
  } else {
    GRPC_ERROR_UNREF(why);
  }
  grpc_winsocket_shutdown(tcp->socket);
  gpr_mu_unlock(&tcp->mu);
  grpc_resource_user_shutdown(exec_ctx, tcp->resource_user);
}

static void win_destroy(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
  grpc_network_status_unregister_endpoint(ep);
  grpc_tcp *tcp = (grpc_tcp *)ep;
  TCP_UNREF(exec_ctx, tcp, "destroy");
}

static char *win_get_peer(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  return gpr_strdup(tcp->peer_string);
}

static grpc_resource_user *win_get_resource_user(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  return tcp->resource_user;
}

static int win_get_fd(grpc_endpoint *ep) { return -1; }

static grpc_endpoint_vtable vtable = {
    win_read,     win_write,   win_add_to_pollset,    win_add_to_pollset_set,
    win_shutdown, win_destroy, win_get_resource_user, win_get_peer,
    win_get_fd};

grpc_endpoint *grpc_tcp_create(grpc_exec_ctx *exec_ctx, grpc_winsocket *socket,
                               grpc_channel_args *channel_args,
                               char *peer_string) {
  grpc_resource_quota *resource_quota = grpc_resource_quota_create(NULL);
  if (channel_args != NULL) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
        grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
        resource_quota = grpc_resource_quota_ref_internal(
            channel_args->args[i].value.pointer.p);
      }
    }
  }
  grpc_tcp *tcp = (grpc_tcp *)gpr_malloc(sizeof(grpc_tcp));
  memset(tcp, 0, sizeof(grpc_tcp));
  tcp->base.vtable = &vtable;
  tcp->socket = socket;
  gpr_mu_init(&tcp->mu);
  gpr_ref_init(&tcp->refcount, 1);
  GRPC_CLOSURE_INIT(&tcp->on_read, on_read, tcp, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&tcp->on_write, on_write, tcp, grpc_schedule_on_exec_ctx);
  tcp->peer_string = gpr_strdup(peer_string);
  tcp->resource_user = grpc_resource_user_create(resource_quota, peer_string);
  /* Tell network status tracking code about the new endpoint */
  grpc_network_status_register_endpoint(&tcp->base);

  return &tcp->base;
}

#endif /* GRPC_WINSOCK_SOCKET */
