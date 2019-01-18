/*
 *
 * Copyright 2016 gRPC authors.
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

#ifdef GRPC_UV
#include <limits.h>
#include <string.h>

#include <grpc/slice_buffer.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/resolve_address_custom.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/iomgr/tcp_custom.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include <uv.h>

#define IGNORE_CONST(addr) ((grpc_sockaddr*)(uintptr_t)(addr))

typedef struct uv_socket_t {
  uv_connect_t connect_req;
  uv_write_t write_req;
  uv_shutdown_t shutdown_req;
  uv_tcp_t* handle;
  uv_buf_t* write_buffers;

  char* read_buf;
  size_t read_len;

  bool pending_connection;
  grpc_custom_socket* accept_socket;
  grpc_error* accept_error;

  grpc_custom_connect_callback connect_cb;
  grpc_custom_write_callback write_cb;
  grpc_custom_read_callback read_cb;
  grpc_custom_accept_callback accept_cb;
  grpc_custom_close_callback close_cb;

} uv_socket_t;

static grpc_error* tcp_error_create(const char* desc, int status) {
  if (status == 0) {
    return GRPC_ERROR_NONE;
  }
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(desc);
  /* All tcp errors are marked with UNAVAILABLE so that application may
   * choose to retry. */
  error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                             GRPC_STATUS_UNAVAILABLE);
  return grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                            grpc_slice_from_static_string(uv_strerror(status)));
}

static void uv_socket_destroy(grpc_custom_socket* socket) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  gpr_free(uv_socket->handle);
  gpr_free(uv_socket);
}

static void alloc_uv_buf(uv_handle_t* handle, size_t suggested_size,
                         uv_buf_t* buf) {
  uv_socket_t* uv_socket =
      (uv_socket_t*)((grpc_custom_socket*)handle->data)->impl;
  (void)suggested_size;
  buf->base = uv_socket->read_buf;
  buf->len = uv_socket->read_len;
}

static void uv_read_callback(uv_stream_t* stream, ssize_t nread,
                             const uv_buf_t* buf) {
  grpc_error* error = GRPC_ERROR_NONE;
  if (nread == 0) {
    // Nothing happened. Wait for the next callback
    return;
  }
  // TODO(murgatroid99): figure out what the return value here means
  uv_read_stop(stream);
  if (nread == UV_EOF) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("EOF");
  } else if (nread < 0) {
    error = tcp_error_create("TCP Read failed", nread);
  }
  grpc_custom_socket* socket = (grpc_custom_socket*)stream->data;
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  uv_socket->read_cb(socket, (size_t)nread, error);
}

static void uv_close_callback(uv_handle_t* handle) {
  grpc_custom_socket* socket = (grpc_custom_socket*)handle->data;
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  if (uv_socket->accept_socket) {
    uv_socket->accept_cb(socket, uv_socket->accept_socket,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("socket closed"));
  }
  uv_socket->close_cb(socket);
}

static void uv_socket_read(grpc_custom_socket* socket, char* buffer,
                           size_t length, grpc_custom_read_callback read_cb) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  int status;
  grpc_error* error;
  uv_socket->read_cb = read_cb;
  uv_socket->read_buf = buffer;
  uv_socket->read_len = length;
  // TODO(murgatroid99): figure out what the return value here means
  status =
      uv_read_start((uv_stream_t*)uv_socket->handle, (uv_alloc_cb)alloc_uv_buf,
                    (uv_read_cb)uv_read_callback);
  if (status != 0) {
    error = tcp_error_create("TCP Read failed at start", status);
    uv_socket->read_cb(socket, 0, error);
  }
}

static void uv_write_callback(uv_write_t* req, int status) {
  grpc_custom_socket* socket = (grpc_custom_socket*)req->data;
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  gpr_free(uv_socket->write_buffers);
  uv_socket->write_cb(socket, tcp_error_create("TCP Write failed", status));
}

void uv_socket_write(grpc_custom_socket* socket,
                     grpc_slice_buffer* write_slices,
                     grpc_custom_write_callback write_cb) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  uv_socket->write_cb = write_cb;
  uv_buf_t* uv_buffers;
  uv_write_t* write_req;

  uv_buffers = (uv_buf_t*)gpr_malloc(sizeof(uv_buf_t) * write_slices->count);
  for (size_t i = 0; i < write_slices->count; i++) {
    uv_buffers[i].base = (char*)GRPC_SLICE_START_PTR(write_slices->slices[i]);
    uv_buffers[i].len = GRPC_SLICE_LENGTH(write_slices->slices[i]);
  }

  uv_socket->write_buffers = uv_buffers;
  write_req = &uv_socket->write_req;
  write_req->data = socket;
  // TODO(murgatroid99): figure out what the return value here means
  uv_write(write_req, (uv_stream_t*)uv_socket->handle, uv_buffers,
           write_slices->count, uv_write_callback);
}

static void shutdown_callback(uv_shutdown_t* req, int status) {}

static void uv_socket_shutdown(grpc_custom_socket* socket) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  uv_shutdown_t* req = &uv_socket->shutdown_req;
  uv_shutdown(req, (uv_stream_t*)uv_socket->handle, shutdown_callback);
}

static void uv_socket_close(grpc_custom_socket* socket,
                            grpc_custom_close_callback close_cb) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  uv_socket->close_cb = close_cb;
  uv_close((uv_handle_t*)uv_socket->handle, uv_close_callback);
}

static grpc_error* uv_socket_init_helper(uv_socket_t* uv_socket, int domain) {
  uv_tcp_t* tcp = (uv_tcp_t*)gpr_malloc(sizeof(uv_tcp_t));
  uv_socket->handle = tcp;
  int status = uv_tcp_init_ex(uv_default_loop(), tcp, (unsigned int)domain);
  if (status != 0) {
    return tcp_error_create("Failed to initialize UV tcp handle", status);
  }
#if defined(GPR_LINUX) && defined(SO_REUSEPORT)
  if (domain == AF_INET || domain == AF_INET6) {
    int enable = 1;
    int fd;
    uv_fileno((uv_handle_t*)tcp, &fd);
    // TODO Handle error here.
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
  }
#endif
  uv_socket->write_buffers = nullptr;
  uv_socket->read_len = 0;
  uv_tcp_nodelay(uv_socket->handle, 1);
  // Node uses a garbage collector to call destructors, so we don't
  // want to hold the uv loop open with active gRPC objects.
  uv_unref((uv_handle_t*)uv_socket->handle);
  uv_socket->pending_connection = false;
  uv_socket->accept_socket = nullptr;
  uv_socket->accept_error = GRPC_ERROR_NONE;
  return GRPC_ERROR_NONE;
}

static grpc_error* uv_socket_init(grpc_custom_socket* socket, int domain) {
  uv_socket_t* uv_socket = (uv_socket_t*)gpr_malloc(sizeof(uv_socket_t));
  grpc_error* error = uv_socket_init_helper(uv_socket, domain);
  if (error != GRPC_ERROR_NONE) {
    return error;
  }
  uv_socket->handle->data = socket;
  socket->impl = uv_socket;
  return GRPC_ERROR_NONE;
}

static grpc_error* uv_socket_getpeername(grpc_custom_socket* socket,
                                         const grpc_sockaddr* addr,
                                         int* addr_len) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  int err = uv_tcp_getpeername(uv_socket->handle,
                               (struct sockaddr*)IGNORE_CONST(addr), addr_len);
  return tcp_error_create("getpeername failed", err);
}

static grpc_error* uv_socket_getsockname(grpc_custom_socket* socket,
                                         const grpc_sockaddr* addr,
                                         int* addr_len) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  int err = uv_tcp_getsockname(uv_socket->handle,
                               (struct sockaddr*)IGNORE_CONST(addr), addr_len);
  return tcp_error_create("getsockname failed", err);
}

static void accept_new_connection(grpc_custom_socket* socket) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  if (!uv_socket->pending_connection || !uv_socket->accept_socket) {
    return;
  }
  grpc_custom_socket* new_socket = uv_socket->accept_socket;
  grpc_error* error = uv_socket->accept_error;
  uv_socket->accept_socket = nullptr;
  uv_socket->accept_error = GRPC_ERROR_NONE;
  uv_socket->pending_connection = false;
  if (uv_socket->accept_error != GRPC_ERROR_NONE) {
    uv_stream_t dummy_handle;
    uv_accept((uv_stream_t*)uv_socket->handle, &dummy_handle);
    uv_socket->accept_cb(socket, new_socket, error);
  } else {
    uv_socket_t* uv_new_socket = (uv_socket_t*)gpr_malloc(sizeof(uv_socket_t));
    uv_socket_init_helper(uv_new_socket, AF_UNSPEC);
    // UV documentation says this is guaranteed to succeed
    GPR_ASSERT(uv_accept((uv_stream_t*)uv_socket->handle,
                         (uv_stream_t*)uv_new_socket->handle) == 0);
    new_socket->impl = uv_new_socket;
    uv_new_socket->handle->data = new_socket;
    uv_socket->accept_cb(socket, new_socket, error);
  }
}

static void uv_on_connect(uv_stream_t* server, int status) {
  grpc_custom_socket* socket = (grpc_custom_socket*)server->data;
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  GPR_ASSERT(!uv_socket->pending_connection);
  uv_socket->pending_connection = true;
  if (status < 0) {
    switch (status) {
      case UV_EINTR:
      case UV_EAGAIN:
        return;
      default:
        uv_socket->accept_error = tcp_error_create("accept failed", status);
    }
  }
  accept_new_connection(socket);
}

void uv_socket_accept(grpc_custom_socket* socket,
                      grpc_custom_socket* new_socket,
                      grpc_custom_accept_callback accept_cb) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  uv_socket->accept_cb = accept_cb;
  GPR_ASSERT(uv_socket->accept_socket == nullptr);
  uv_socket->accept_socket = new_socket;
  accept_new_connection(socket);
}

static grpc_error* uv_socket_bind(grpc_custom_socket* socket,
                                  const grpc_sockaddr* addr, size_t len,
                                  int flags) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  int status =
      uv_tcp_bind((uv_tcp_t*)uv_socket->handle, (struct sockaddr*)addr, 0);
  return tcp_error_create("Failed to bind to port", status);
}

static grpc_error* uv_socket_listen(grpc_custom_socket* socket) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  int status =
      uv_listen((uv_stream_t*)uv_socket->handle, SOMAXCONN, uv_on_connect);
  return tcp_error_create("Failed to listen to port", status);
}

static void uv_tc_on_connect(uv_connect_t* req, int status) {
  grpc_custom_socket* socket = (grpc_custom_socket*)req->data;
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  grpc_error* error;
  if (status == UV_ECANCELED) {
    // This should only happen if the handle is already closed
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Timeout occurred");
  } else {
    error = tcp_error_create("Failed to connect to remote host", status);
  }
  uv_socket->connect_cb(socket, error);
}

static void uv_socket_connect(grpc_custom_socket* socket,
                              const grpc_sockaddr* addr, size_t len,
                              grpc_custom_connect_callback connect_cb) {
  uv_socket_t* uv_socket = (uv_socket_t*)socket->impl;
  uv_socket->connect_cb = connect_cb;
  uv_socket->connect_req.data = socket;
  int status = uv_tcp_connect(&uv_socket->connect_req, uv_socket->handle,
                              (struct sockaddr*)addr, uv_tc_on_connect);
  if (status != 0) {
    // The callback will not be called
    uv_socket->connect_cb(socket, tcp_error_create("connect failed", status));
  }
}

static grpc_resolved_addresses* handle_addrinfo_result(
    struct addrinfo* result) {
  struct addrinfo* resp;
  size_t i;
  grpc_resolved_addresses* addresses =
      (grpc_resolved_addresses*)gpr_malloc(sizeof(grpc_resolved_addresses));
  addresses->naddrs = 0;
  for (resp = result; resp != nullptr; resp = resp->ai_next) {
    addresses->naddrs++;
  }
  addresses->addrs = (grpc_resolved_address*)gpr_malloc(
      sizeof(grpc_resolved_address) * addresses->naddrs);
  for (resp = result, i = 0; resp != nullptr; resp = resp->ai_next, i++) {
    memcpy(&addresses->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
    addresses->addrs[i].len = resp->ai_addrlen;
  }
  // addrinfo objects are allocated by libuv (e.g. in uv_getaddrinfo)
  // and not by gpr_malloc
  uv_freeaddrinfo(result);
  return addresses;
}

static void uv_resolve_callback(uv_getaddrinfo_t* req, int status,
                                struct addrinfo* res) {
  grpc_custom_resolver* r = (grpc_custom_resolver*)req->data;
  gpr_free(req);
  grpc_resolved_addresses* result = nullptr;
  if (status == 0) {
    result = handle_addrinfo_result(res);
  }
  grpc_custom_resolve_callback(r, result,
                               tcp_error_create("getaddrinfo failed", status));
}

static grpc_error* uv_resolve(char* host, char* port,
                              grpc_resolved_addresses** result) {
  int status;
  uv_getaddrinfo_t req;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = SOCK_STREAM; /* stream socket */
  hints.ai_flags = AI_PASSIVE;     /* for wildcard IP address */
  status = uv_getaddrinfo(uv_default_loop(), &req, NULL, host, port, &hints);
  if (status != 0) {
    *result = nullptr;
  } else {
    *result = handle_addrinfo_result(req.addrinfo);
  }
  return tcp_error_create("getaddrinfo failed", status);
}

static void uv_resolve_async(grpc_custom_resolver* r, char* host, char* port) {
  int status;
  uv_getaddrinfo_t* req =
      (uv_getaddrinfo_t*)gpr_malloc(sizeof(uv_getaddrinfo_t));
  req->data = r;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = GRPC_AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = GRPC_SOCK_STREAM; /* stream socket */
  hints.ai_flags = GRPC_AI_PASSIVE;     /* for wildcard IP address */
  status = uv_getaddrinfo(uv_default_loop(), req, uv_resolve_callback, host,
                          port, &hints);
  if (status != 0) {
    gpr_free(req);
    grpc_error* error = tcp_error_create("getaddrinfo failed", status);
    grpc_custom_resolve_callback(r, NULL, error);
  }
}

grpc_custom_resolver_vtable uv_resolver_vtable = {uv_resolve, uv_resolve_async};

grpc_socket_vtable grpc_uv_socket_vtable = {
    uv_socket_init,     uv_socket_connect,     uv_socket_destroy,
    uv_socket_shutdown, uv_socket_close,       uv_socket_write,
    uv_socket_read,     uv_socket_getpeername, uv_socket_getsockname,
    uv_socket_bind,     uv_socket_listen,      uv_socket_accept};

#endif
