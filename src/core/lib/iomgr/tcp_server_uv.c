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

#ifdef GRPC_UV

#include <assert.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_uv.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/tcp_uv.h"

/* one listening port */
typedef struct grpc_tcp_listener grpc_tcp_listener;
struct grpc_tcp_listener {
  uv_tcp_t *handle;
  grpc_tcp_server *server;
  unsigned port_index;
  int port;
  /* linked list */
  struct grpc_tcp_listener *next;

  bool closed;

  bool has_pending_connection;
};

struct grpc_tcp_server {
  gpr_refcount refs;

  /* Called whenever accept() succeeds on a server port. */
  grpc_tcp_server_cb on_accept_cb;
  void *on_accept_cb_arg;

  int open_ports;

  /* linked list of server ports */
  grpc_tcp_listener *head;
  grpc_tcp_listener *tail;

  /* List of closures passed to shutdown_starting_add(). */
  grpc_closure_list shutdown_starting;

  /* shutdown callback */
  grpc_closure *shutdown_complete;

  bool shutdown;

  grpc_resource_quota *resource_quota;
};

grpc_error *grpc_tcp_server_create(grpc_exec_ctx *exec_ctx,
                                   grpc_closure *shutdown_complete,
                                   const grpc_channel_args *args,
                                   grpc_tcp_server **server) {
  grpc_tcp_server *s = gpr_malloc(sizeof(grpc_tcp_server));
  s->resource_quota = grpc_resource_quota_create(NULL);
  for (size_t i = 0; i < (args == NULL ? 0 : args->num_args); i++) {
    if (0 == strcmp(GRPC_ARG_RESOURCE_QUOTA, args->args[i].key)) {
      if (args->args[i].type == GRPC_ARG_POINTER) {
        grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);
        s->resource_quota =
            grpc_resource_quota_ref_internal(args->args[i].value.pointer.p);
      } else {
        grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);
        gpr_free(s);
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            GRPC_ARG_RESOURCE_QUOTA " must be a pointer to a buffer pool");
      }
    }
  }
  gpr_ref_init(&s->refs, 1);
  s->on_accept_cb = NULL;
  s->on_accept_cb_arg = NULL;
  s->open_ports = 0;
  s->head = NULL;
  s->tail = NULL;
  s->shutdown_starting.head = NULL;
  s->shutdown_starting.tail = NULL;
  s->shutdown_complete = shutdown_complete;
  s->shutdown = false;
  *server = s;
  return GRPC_ERROR_NONE;
}

grpc_tcp_server *grpc_tcp_server_ref(grpc_tcp_server *s) {
  GRPC_UV_ASSERT_SAME_THREAD();
  gpr_ref(&s->refs);
  return s;
}

void grpc_tcp_server_shutdown_starting_add(grpc_tcp_server *s,
                                           grpc_closure *shutdown_starting) {
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting,
                           GRPC_ERROR_NONE);
}

static void finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  GPR_ASSERT(s->shutdown);
  if (s->shutdown_complete != NULL) {
    GRPC_CLOSURE_SCHED(exec_ctx, s->shutdown_complete, GRPC_ERROR_NONE);
  }

  while (s->head) {
    grpc_tcp_listener *sp = s->head;
    s->head = sp->next;
    sp->next = NULL;
    gpr_free(sp->handle);
    gpr_free(sp);
  }
  grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);
  gpr_free(s);
}

static void handle_close_callback(uv_handle_t *handle) {
  grpc_tcp_listener *sp = (grpc_tcp_listener *)handle->data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  sp->server->open_ports--;
  if (sp->server->open_ports == 0 && sp->server->shutdown) {
    finish_shutdown(&exec_ctx, sp->server);
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

static void close_listener(grpc_tcp_listener *sp) {
  if (!sp->closed) {
    sp->closed = true;
    uv_close((uv_handle_t *)sp->handle, handle_close_callback);
  }
}

static void tcp_server_destroy(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  int immediately_done = 0;
  grpc_tcp_listener *sp;

  GPR_ASSERT(!s->shutdown);
  s->shutdown = true;

  if (s->open_ports == 0) {
    immediately_done = 1;
  }
  for (sp = s->head; sp; sp = sp->next) {
    close_listener(sp);
  }

  if (immediately_done) {
    finish_shutdown(exec_ctx, s);
  }
}

void grpc_tcp_server_unref(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  GRPC_UV_ASSERT_SAME_THREAD();
  if (gpr_unref(&s->refs)) {
    /* Complete shutdown_starting work before destroying. */
    grpc_exec_ctx local_exec_ctx = GRPC_EXEC_CTX_INIT;
    GRPC_CLOSURE_LIST_SCHED(&local_exec_ctx, &s->shutdown_starting);
    if (exec_ctx == NULL) {
      grpc_exec_ctx_flush(&local_exec_ctx);
      tcp_server_destroy(&local_exec_ctx, s);
      grpc_exec_ctx_finish(&local_exec_ctx);
    } else {
      grpc_exec_ctx_finish(&local_exec_ctx);
      tcp_server_destroy(exec_ctx, s);
    }
  }
}

static void finish_accept(grpc_exec_ctx *exec_ctx, grpc_tcp_listener *sp) {
  grpc_tcp_server_acceptor *acceptor = gpr_malloc(sizeof(*acceptor));
  uv_tcp_t *client;
  grpc_endpoint *ep = NULL;
  grpc_resolved_address peer_name;
  char *peer_name_string;
  int err;
  uv_tcp_t *server = sp->handle;

  client = gpr_malloc(sizeof(uv_tcp_t));
  uv_tcp_init(uv_default_loop(), client);
  // UV documentation says this is guaranteed to succeed
  uv_accept((uv_stream_t *)server, (uv_stream_t *)client);
  peer_name_string = NULL;
  memset(&peer_name, 0, sizeof(grpc_resolved_address));
  peer_name.len = sizeof(struct sockaddr_storage);
  err = uv_tcp_getpeername(client, (struct sockaddr *)&peer_name.addr,
                           (int *)&peer_name.len);
  if (err == 0) {
    peer_name_string = grpc_sockaddr_to_uri(&peer_name);
  } else {
    gpr_log(GPR_INFO, "uv_tcp_getpeername error: %s", uv_strerror(err));
  }
  if (GRPC_TRACER_ON(grpc_tcp_trace)) {
    if (peer_name_string) {
      gpr_log(GPR_DEBUG, "SERVER_CONNECT: %p accepted connection: %s",
              sp->server, peer_name_string);
    } else {
      gpr_log(GPR_DEBUG, "SERVER_CONNECT: %p accepted connection", sp->server);
    }
  }
  ep = grpc_tcp_create(client, sp->server->resource_quota, peer_name_string);
  acceptor->from_server = sp->server;
  acceptor->port_index = sp->port_index;
  acceptor->fd_index = 0;
  sp->server->on_accept_cb(exec_ctx, sp->server->on_accept_cb_arg, ep, NULL,
                           acceptor);
  gpr_free(peer_name_string);
}

static void on_connect(uv_stream_t *server, int status) {
  grpc_tcp_listener *sp = (grpc_tcp_listener *)server->data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  if (status < 0) {
    switch (status) {
      case UV_EINTR:
      case UV_EAGAIN:
        return;
      default:
        close_listener(sp);
        return;
    }
  }

  GPR_ASSERT(!sp->has_pending_connection);

  if (GRPC_TRACER_ON(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "SERVER_CONNECT: %p incoming connection", sp->server);
  }

  // Create acceptor.
  if (sp->server->on_accept_cb) {
    finish_accept(&exec_ctx, sp);
  } else {
    sp->has_pending_connection = true;
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

static grpc_error *add_socket_to_server(grpc_tcp_server *s, uv_tcp_t *handle,
                                        const grpc_resolved_address *addr,
                                        unsigned port_index,
                                        grpc_tcp_listener **listener) {
  grpc_tcp_listener *sp = NULL;
  int port = -1;
  int status;
  grpc_error *error;
  grpc_resolved_address sockname_temp;

  // The last argument to uv_tcp_bind is flags
  status = uv_tcp_bind(handle, (struct sockaddr *)addr->addr, 0);
  if (status != 0) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to bind to port");
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
    return error;
  }

  status = uv_listen((uv_stream_t *)handle, SOMAXCONN, on_connect);
  if (status != 0) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to listen to port");
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
    return error;
  }

  sockname_temp.len = (int)sizeof(struct sockaddr_storage);
  status = uv_tcp_getsockname(handle, (struct sockaddr *)&sockname_temp.addr,
                              (int *)&sockname_temp.len);
  if (status != 0) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("getsockname failed");
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
    return error;
  }

  port = grpc_sockaddr_get_port(&sockname_temp);

  GPR_ASSERT(port >= 0);
  GPR_ASSERT(!s->on_accept_cb && "must add ports before starting server");
  sp = gpr_zalloc(sizeof(grpc_tcp_listener));
  sp->next = NULL;
  if (s->head == NULL) {
    s->head = sp;
  } else {
    s->tail->next = sp;
  }
  s->tail = sp;
  sp->server = s;
  sp->handle = handle;
  sp->port = port;
  sp->port_index = port_index;
  sp->closed = false;
  handle->data = sp;
  s->open_ports++;
  GPR_ASSERT(sp->handle);
  *listener = sp;

  return GRPC_ERROR_NONE;
}

grpc_error *grpc_tcp_server_add_port(grpc_tcp_server *s,
                                     const grpc_resolved_address *addr,
                                     int *port) {
  // This function is mostly copied from tcp_server_windows.c
  grpc_tcp_listener *sp = NULL;
  uv_tcp_t *handle;
  grpc_resolved_address addr6_v4mapped;
  grpc_resolved_address wildcard;
  grpc_resolved_address *allocated_addr = NULL;
  grpc_resolved_address sockname_temp;
  unsigned port_index = 0;
  int status;
  grpc_error *error = GRPC_ERROR_NONE;
  int family;

  GRPC_UV_ASSERT_SAME_THREAD();

  if (s->tail != NULL) {
    port_index = s->tail->port_index + 1;
  }

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (sp = s->head; sp; sp = sp->next) {
      sockname_temp.len = sizeof(struct sockaddr_storage);
      if (0 == uv_tcp_getsockname(sp->handle,
                                  (struct sockaddr *)&sockname_temp.addr,
                                  (int *)&sockname_temp.len)) {
        *port = grpc_sockaddr_get_port(&sockname_temp);
        if (*port > 0) {
          allocated_addr = gpr_malloc(sizeof(grpc_resolved_address));
          memcpy(allocated_addr, addr, sizeof(grpc_resolved_address));
          grpc_sockaddr_set_port(allocated_addr, *port);
          addr = allocated_addr;
          break;
        }
      }
    }
  }

  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }

  /* Treat :: or 0.0.0.0 as a family-agnostic wildcard. */
  if (grpc_sockaddr_is_wildcard(addr, port)) {
    grpc_sockaddr_make_wildcard6(*port, &wildcard);

    addr = &wildcard;
  }

  handle = gpr_malloc(sizeof(uv_tcp_t));

  family = grpc_sockaddr_get_family(addr);
  status = uv_tcp_init_ex(uv_default_loop(), handle, (unsigned int)family);
#if defined(GPR_LINUX) && defined(SO_REUSEPORT)
  if (family == AF_INET || family == AF_INET6) {
    int fd;
    uv_fileno((uv_handle_t *)handle, &fd);
    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
  }
#endif /* GPR_LINUX && SO_REUSEPORT */

  if (status == 0) {
    error = add_socket_to_server(s, handle, addr, port_index, &sp);
  } else {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Failed to initialize UV tcp handle");
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
  }

  gpr_free(allocated_addr);

  if (GRPC_TRACER_ON(grpc_tcp_trace)) {
    char *port_string;
    grpc_sockaddr_to_string(&port_string, addr, 0);
    const char *str = grpc_error_string(error);
    if (port_string) {
      gpr_log(GPR_DEBUG, "SERVER %p add_port %s error=%s", s, port_string, str);
      gpr_free(port_string);
    } else {
      gpr_log(GPR_DEBUG, "SERVER %p add_port error=%s", s, str);
    }
  }

  if (error != GRPC_ERROR_NONE) {
    grpc_error *error_out = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Failed to add port to server", &error, 1);
    GRPC_ERROR_UNREF(error);
    error = error_out;
    *port = -1;
  } else {
    GPR_ASSERT(sp != NULL);
    *port = sp->port;
  }
  return error;
}

void grpc_tcp_server_start(grpc_exec_ctx *exec_ctx, grpc_tcp_server *server,
                           grpc_pollset **pollsets, size_t pollset_count,
                           grpc_tcp_server_cb on_accept_cb, void *cb_arg) {
  grpc_tcp_listener *sp;
  (void)pollsets;
  (void)pollset_count;
  GRPC_UV_ASSERT_SAME_THREAD();
  if (GRPC_TRACER_ON(grpc_tcp_trace)) {
    gpr_log(GPR_DEBUG, "SERVER_START %p", server);
  }
  GPR_ASSERT(on_accept_cb);
  GPR_ASSERT(!server->on_accept_cb);
  server->on_accept_cb = on_accept_cb;
  server->on_accept_cb_arg = cb_arg;
  for (sp = server->head; sp; sp = sp->next) {
    if (sp->has_pending_connection) {
      finish_accept(exec_ctx, sp);
      sp->has_pending_connection = false;
    }
  }
}

void grpc_tcp_server_shutdown_listeners(grpc_exec_ctx *exec_ctx,
                                        grpc_tcp_server *s) {}

#endif /* GRPC_UV */
