/*
 *
 * Copyright 2017 gRPC authors.
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

#ifdef GRPC_POSIX_SOCKET_TCP_SERVER_UTILS_COMMON

#include "src/core/lib/iomgr/tcp_server_utils_posix.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"

#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

static gpr_once s_init_max_accept_queue_size = GPR_ONCE_INIT;
static int s_max_accept_queue_size;

/* get max listen queue size on linux */
static void init_max_accept_queue_size(void) {
  int n = SOMAXCONN;
  char buf[64];
  FILE* fp = fopen("/proc/sys/net/core/somaxconn", "r");
  if (fp == nullptr) {
    /* 2.4 kernel. */
    s_max_accept_queue_size = SOMAXCONN;
    return;
  }
  if (fgets(buf, sizeof buf, fp)) {
    char* end;
    long i = strtol(buf, &end, 10);
    if (i > 0 && i <= INT_MAX && end && *end == '\n') {
      n = static_cast<int>(i);
    }
  }
  fclose(fp);
  s_max_accept_queue_size = n;

  if (s_max_accept_queue_size < MIN_SAFE_ACCEPT_QUEUE_SIZE) {
    gpr_log(GPR_INFO,
            "Suspiciously small accept queue (%d) will probably lead to "
            "connection drops",
            s_max_accept_queue_size);
  }
}

static int get_max_accept_queue_size(void) {
  gpr_once_init(&s_init_max_accept_queue_size, init_max_accept_queue_size);
  return s_max_accept_queue_size;
}

static grpc_error* add_socket_to_server(grpc_tcp_server* s, int fd,
                                        const grpc_resolved_address* addr,
                                        unsigned port_index, unsigned fd_index,
                                        grpc_tcp_listener** listener) {
  grpc_tcp_listener* sp = nullptr;
  int port = -1;
  char* addr_str;
  char* name;

  grpc_error* err =
      grpc_tcp_server_prepare_socket(s, fd, addr, s->so_reuseport, &port);
  if (err == GRPC_ERROR_NONE) {
    GPR_ASSERT(port > 0);
    grpc_sockaddr_to_string(&addr_str, addr, 1);
    gpr_asprintf(&name, "tcp-server-listener:%s", addr_str);
    gpr_mu_lock(&s->mu);
    s->nports++;
    GPR_ASSERT(!s->on_accept_cb && "must add ports before starting server");
    sp = static_cast<grpc_tcp_listener*>(gpr_malloc(sizeof(grpc_tcp_listener)));
    sp->next = nullptr;
    if (s->head == nullptr) {
      s->head = sp;
    } else {
      s->tail->next = sp;
    }
    s->tail = sp;
    sp->server = s;
    sp->fd = fd;
    sp->emfd = grpc_fd_create(fd, name);
    memcpy(&sp->addr, addr, sizeof(grpc_resolved_address));
    sp->port = port;
    sp->port_index = port_index;
    sp->fd_index = fd_index;
    sp->is_sibling = 0;
    sp->sibling = nullptr;
    GPR_ASSERT(sp->emfd);
    gpr_mu_unlock(&s->mu);
    gpr_free(addr_str);
    gpr_free(name);
  }

  *listener = sp;
  return err;
}

/* If successful, add a listener to s for addr, set *dsmode for the socket, and
   return the *listener. */
grpc_error* grpc_tcp_server_add_addr(grpc_tcp_server* s,
                                     const grpc_resolved_address* addr,
                                     unsigned port_index, unsigned fd_index,
                                     grpc_dualstack_mode* dsmode,
                                     grpc_tcp_listener** listener) {
  grpc_resolved_address addr4_copy;
  int fd;
  grpc_error* err =
      grpc_create_dualstack_socket(addr, SOCK_STREAM, 0, dsmode, &fd);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }
  if (*dsmode == GRPC_DSMODE_IPV4 &&
      grpc_sockaddr_is_v4mapped(addr, &addr4_copy)) {
    addr = &addr4_copy;
  }
  return add_socket_to_server(s, fd, addr, port_index, fd_index, listener);
}

/* Prepare a recently-created socket for listening. */
grpc_error* grpc_tcp_server_prepare_socket(grpc_tcp_server* s, int fd,
                                           const grpc_resolved_address* addr,
                                           bool so_reuseport, int* port) {
  grpc_resolved_address sockname_temp;
  grpc_error* err = GRPC_ERROR_NONE;

  GPR_ASSERT(fd >= 0);

  if (so_reuseport && !grpc_is_unix_socket(addr)) {
    err = grpc_set_socket_reuse_port(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
  }

  err = grpc_set_socket_nonblocking(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  err = grpc_set_socket_cloexec(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  if (!grpc_is_unix_socket(addr)) {
    err = grpc_set_socket_low_latency(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
    err = grpc_set_socket_reuse_addr(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
  }
  err = grpc_set_socket_no_sigpipe_if_possible(fd);
  if (err != GRPC_ERROR_NONE) goto error;

  if (s->channel_args) {
    for (size_t i = 0; i < s->channel_args->num_args; i++) {
      if (0 == strcmp(s->channel_args->args[i].key, GRPC_ARG_SOCKET_MUTATOR)) {
        GPR_ASSERT(s->channel_args->args[i].type == GRPC_ARG_POINTER);
        grpc_socket_mutator* mutator = static_cast<grpc_socket_mutator*>(
            s->channel_args->args[i].value.pointer.p);
        err = grpc_set_socket_with_mutator(fd, mutator);
        if (err != GRPC_ERROR_NONE) goto error;
      }
    }
  }

  if (bind(fd, reinterpret_cast<grpc_sockaddr*>(const_cast<char*>(addr->addr)),
           addr->len) < 0) {
    err = GRPC_OS_ERROR(errno, "bind");
    goto error;
  }

  if (listen(fd, get_max_accept_queue_size()) < 0) {
    err = GRPC_OS_ERROR(errno, "listen");
    goto error;
  }

  sockname_temp.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));

  if (getsockname(fd, reinterpret_cast<grpc_sockaddr*>(sockname_temp.addr),
                  &sockname_temp.len) < 0) {
    err = GRPC_OS_ERROR(errno, "getsockname");
    goto error;
  }

  *port = grpc_sockaddr_get_port(&sockname_temp);
  return GRPC_ERROR_NONE;

error:
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  if (fd >= 0) {
    close(fd);
  }
  grpc_error* ret =
      grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Unable to configure socket", &err, 1),
                         GRPC_ERROR_INT_FD, fd);
  GRPC_ERROR_UNREF(err);
  return ret;
}

#endif /* GRPC_POSIX_SOCKET_TCP_SERVER_UTILS_COMMON */
