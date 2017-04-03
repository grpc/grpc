/*
 *
 * Copyright 2017, Google Inc.
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

#ifdef GRPC_POSIX_SOCKET

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

static gpr_once s_init_max_accept_queue_size;
static int s_max_accept_queue_size;

/* get max listen queue size on linux */
static void init_max_accept_queue_size(void) {
  int n = SOMAXCONN;
  char buf[64];
  FILE *fp = fopen("/proc/sys/net/core/somaxconn", "r");
  if (fp == NULL) {
    /* 2.4 kernel. */
    s_max_accept_queue_size = SOMAXCONN;
    return;
  }
  if (fgets(buf, sizeof buf, fp)) {
    char *end;
    long i = strtol(buf, &end, 10);
    if (i > 0 && i <= INT_MAX && end && *end == 0) {
      n = (int)i;
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

static grpc_error *add_socket_to_server(grpc_tcp_server *s, int fd,
                                        const grpc_resolved_address *addr,
                                        unsigned port_index, unsigned fd_index,
                                        grpc_tcp_listener **listener) {
  grpc_tcp_listener *sp = NULL;
  int port = -1;
  char *addr_str;
  char *name;

  grpc_error *err =
      grpc_tcp_server_prepare_socket(fd, addr, s->so_reuseport, &port);
  if (err == GRPC_ERROR_NONE) {
    GPR_ASSERT(port > 0);
    grpc_sockaddr_to_string(&addr_str, addr, 1);
    gpr_asprintf(&name, "tcp-server-listener:%s", addr_str);
    gpr_mu_lock(&s->mu);
    s->nports++;
    GPR_ASSERT(!s->on_accept_cb && "must add ports before starting server");
    sp = gpr_malloc(sizeof(grpc_tcp_listener));
    sp->next = NULL;
    if (s->head == NULL) {
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
    sp->sibling = NULL;
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
grpc_error *grpc_tcp_server_add_addr(grpc_tcp_server *s,
                                     const grpc_resolved_address *addr,
                                     unsigned port_index, unsigned fd_index,
                                     grpc_dualstack_mode *dsmode,
                                     grpc_tcp_listener **listener) {
  grpc_resolved_address addr4_copy;
  int fd;
  grpc_error *err =
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
grpc_error *grpc_tcp_server_prepare_socket(int fd,
                                           const grpc_resolved_address *addr,
                                           bool so_reuseport, int *port) {
  grpc_resolved_address sockname_temp;
  grpc_error *err = GRPC_ERROR_NONE;

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

  GPR_ASSERT(addr->len < ~(socklen_t)0);
  if (bind(fd, (struct sockaddr *)addr->addr, (socklen_t)addr->len) < 0) {
    err = GRPC_OS_ERROR(errno, "bind");
    goto error;
  }

  if (listen(fd, get_max_accept_queue_size()) < 0) {
    err = GRPC_OS_ERROR(errno, "listen");
    goto error;
  }

  sockname_temp.len = sizeof(struct sockaddr_storage);

  if (getsockname(fd, (struct sockaddr *)sockname_temp.addr,
                  (socklen_t *)&sockname_temp.len) < 0) {
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
  grpc_error *ret =
      grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Unable to configure socket", &err, 1),
                         GRPC_ERROR_INT_FD, fd);
  GRPC_ERROR_UNREF(err);
  return ret;
}

#endif /* GRPC_POSIX_SOCKET */
