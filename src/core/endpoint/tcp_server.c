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

#define _GNU_SOURCE
#include "src/core/endpoint/tcp_server.h"

#include <limits.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "src/core/endpoint/socket_utils.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#define INIT_PORT_CAP 2
#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

static gpr_once s_init_max_accept_queue_size;
static int s_max_accept_queue_size;

/* one listening port */
typedef struct {
  int fd;
  grpc_em_fd *emfd;
  grpc_tcp_server *server;
} server_port;

/* the overall server */
struct grpc_tcp_server {
  grpc_em *em;
  grpc_tcp_server_cb cb;
  void *cb_arg;

  gpr_mu mu;
  gpr_cv cv;

  /* active port count: how many ports are actually still listening */
  int active_ports;

  /* all listening ports */
  server_port *ports;
  size_t nports;
  size_t port_capacity;
};

grpc_tcp_server *grpc_tcp_server_create(grpc_em *em) {
  grpc_tcp_server *s = gpr_malloc(sizeof(grpc_tcp_server));
  gpr_mu_init(&s->mu);
  gpr_cv_init(&s->cv);
  s->active_ports = 0;
  s->em = em;
  s->cb = NULL;
  s->cb_arg = NULL;
  s->ports = gpr_malloc(sizeof(server_port) * INIT_PORT_CAP);
  s->nports = 0;
  s->port_capacity = INIT_PORT_CAP;
  return s;
}

void grpc_tcp_server_destroy(grpc_tcp_server *s) {
  size_t i;
  gpr_mu_lock(&s->mu);
  /* shutdown all fd's */
  for (i = 0; i < s->nports; i++) {
    grpc_em_fd_shutdown(s->ports[i].emfd);
  }
  /* wait while that happens */
  while (s->active_ports) {
    gpr_cv_wait(&s->cv, &s->mu, gpr_inf_future);
  }
  gpr_mu_unlock(&s->mu);

  /* delete ALL the things */
  for (i = 0; i < s->nports; i++) {
    server_port *sp = &s->ports[i];
    grpc_em_fd_destroy(sp->emfd);
    gpr_free(sp->emfd);
  }
  gpr_free(s->ports);
  gpr_free(s);
}

/* get max listen queue size on linux */
static void init_max_accept_queue_size() {
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
      n = i;
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

static int get_max_accept_queue_size() {
  gpr_once_init(&s_init_max_accept_queue_size, init_max_accept_queue_size);
  return s_max_accept_queue_size;
}

/* Prepare a recently-created socket for listening. */
static int prepare_socket(int fd, const struct sockaddr *addr, int addr_len) {
  if (fd < 0) {
    goto error;
  }

  if (!grpc_set_socket_nonblocking(fd, 1) || !grpc_set_socket_cloexec(fd, 1) ||
      !grpc_set_socket_low_latency(fd, 1) ||
      !grpc_set_socket_reuse_addr(fd, 1)) {
    gpr_log(GPR_ERROR, "Unable to configure socket %d: %s", fd,
            strerror(errno));
    goto error;
  }

  if (bind(fd, addr, addr_len) < 0) {
    char *addr_str;
    grpc_sockaddr_to_string(&addr_str, addr, 0);
    gpr_log(GPR_ERROR, "bind addr=%s: %s", addr_str, strerror(errno));
    gpr_free(addr_str);
    goto error;
  }

  if (listen(fd, get_max_accept_queue_size()) < 0) {
    gpr_log(GPR_ERROR, "listen: %s", strerror(errno));
    goto error;
  }

  return 1;

error:
  if (fd >= 0) {
    close(fd);
  }
  return 0;
}

/* event manager callback when reads are ready */
static void on_read(void *arg, grpc_em_cb_status status) {
  server_port *sp = arg;

  if (status != GRPC_CALLBACK_SUCCESS) {
    goto error;
  }

  /* loop until accept4 returns EAGAIN, and then re-arm notification */
  for (;;) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    /* Note: If we ever decide to return this address to the user, remember to
             strip off the ::ffff:0.0.0.0/96 prefix first. */
    int fd = grpc_accept4(sp->fd, (struct sockaddr *)&addr, &addrlen, 1, 1);
    if (fd < 0) {
      switch (errno) {
        case EINTR:
          continue;
        case EAGAIN:
          if (GRPC_EM_OK != grpc_em_fd_notify_on_read(sp->emfd, on_read, sp,
                                                      gpr_inf_future)) {
            gpr_log(GPR_ERROR, "Failed to register read request with em");
            goto error;
          }
          return;
        default:
          gpr_log(GPR_ERROR, "Failed accept4: %s", strerror(errno));
          goto error;
      }
    }

    sp->server->cb(sp->server->cb_arg, grpc_tcp_create(fd, sp->server->em));
  }

  abort();

error:
  gpr_mu_lock(&sp->server->mu);
  if (0 == --sp->server->active_ports) {
    gpr_cv_broadcast(&sp->server->cv);
  }
  gpr_mu_unlock(&sp->server->mu);
}

static int add_socket_to_server(grpc_tcp_server *s, int fd,
                                const struct sockaddr *addr, int addr_len) {
  server_port *sp;

  if (!prepare_socket(fd, addr, addr_len)) {
    return 0;
  }

  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->cb && "must add ports before starting server");
  /* append it to the list under a lock */
  if (s->nports == s->port_capacity) {
    s->port_capacity *= 2;
    s->ports = gpr_realloc(s->ports, sizeof(server_port *) * s->port_capacity);
  }
  sp = &s->ports[s->nports++];
  sp->emfd = gpr_malloc(sizeof(grpc_em_fd));
  sp->fd = fd;
  sp->server = s;
  /* initialize the em desc */
  if (GRPC_EM_OK != grpc_em_fd_init(sp->emfd, s->em, fd)) {
    grpc_em_fd_destroy(sp->emfd);
    gpr_free(sp->emfd);
    s->nports--;
    gpr_mu_unlock(&s->mu);
    return 0;
  }
  gpr_mu_unlock(&s->mu);

  return 1;
}

int grpc_tcp_server_add_port(grpc_tcp_server *s, const struct sockaddr *addr,
                             int addr_len) {
  int ok = 0;
  int fd;
  grpc_dualstack_mode dsmode;
  struct sockaddr_in6 addr6_v4mapped;
  struct sockaddr_in wild4;
  struct sockaddr_in6 wild6;
  struct sockaddr_in addr4_copy;
  int port;

  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = (const struct sockaddr *)&addr6_v4mapped;
    addr_len = sizeof(addr6_v4mapped);
  }

  /* Treat :: or 0.0.0.0 as a family-agnostic wildcard. */
  if (grpc_sockaddr_is_wildcard(addr, &port)) {
    grpc_sockaddr_make_wildcards(port, &wild4, &wild6);

    /* Try listening on IPv6 first. */
    addr = (struct sockaddr *)&wild6;
    addr_len = sizeof(wild6);
    fd = grpc_create_dualstack_socket(addr, SOCK_STREAM, 0, &dsmode);
    ok |= add_socket_to_server(s, fd, addr, addr_len);
    if (fd >= 0 && dsmode == GRPC_DSMODE_DUALSTACK) {
      return ok;
    }

    /* If we didn't get a dualstack socket, also listen on 0.0.0.0. */
    addr = (struct sockaddr *)&wild4;
    addr_len = sizeof(wild4);
  }

  fd = grpc_create_dualstack_socket(addr, SOCK_STREAM, 0, &dsmode);
  if (fd < 0) {
    gpr_log(GPR_ERROR, "Unable to create socket: %s", strerror(errno));
  }
  if (dsmode == GRPC_DSMODE_IPV4 &&
      grpc_sockaddr_is_v4mapped(addr, &addr4_copy)) {
    addr = (struct sockaddr *)&addr4_copy;
    addr_len = sizeof(addr4_copy);
  }
  ok |= add_socket_to_server(s, fd, addr, addr_len);
  return ok;
}

int grpc_tcp_server_get_fd(grpc_tcp_server *s, int index) {
  return (0 <= index && index < s->nports) ? s->ports[index].fd : -1;
}

void grpc_tcp_server_start(grpc_tcp_server *s, grpc_tcp_server_cb cb,
                           void *cb_arg) {
  size_t i;
  GPR_ASSERT(cb);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->cb);
  GPR_ASSERT(s->active_ports == 0);
  s->cb = cb;
  s->cb_arg = cb_arg;
  for (i = 0; i < s->nports; i++) {
    grpc_em_fd_notify_on_read(s->ports[i].emfd, on_read, &s->ports[i],
                              gpr_inf_future);
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);
}
