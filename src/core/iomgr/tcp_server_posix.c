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

/* FIXME: "posix" files shouldn't be depending on _GNU_SOURCE */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/iomgr/tcp_server.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "src/core/iomgr/pollset_posix.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/iomgr/sockaddr_utils.h"
#include "src/core/iomgr/socket_utils_posix.h"
#include "src/core/iomgr/tcp_posix.h"
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
  grpc_fd *emfd;
  grpc_tcp_server *server;
  union {
    gpr_uint8 untyped[GRPC_MAX_SOCKADDR_SIZE];
    struct sockaddr sockaddr;
    struct sockaddr_un un;
  } addr;
  int addr_len;
  grpc_iomgr_closure read_closure;
} server_port;

static void unlink_if_unix_domain_socket(const struct sockaddr_un *un) {
  struct stat st;

  if (stat(un->sun_path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFSOCK) {
    unlink(un->sun_path);
  }
}

/* the overall server */
struct grpc_tcp_server {
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

grpc_tcp_server *grpc_tcp_server_create(void) {
  grpc_tcp_server *s = gpr_malloc(sizeof(grpc_tcp_server));
  gpr_mu_init(&s->mu);
  gpr_cv_init(&s->cv);
  s->active_ports = 0;
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
    grpc_fd_shutdown(s->ports[i].emfd);
  }
  /* wait while that happens */
  while (s->active_ports) {
    gpr_cv_wait(&s->cv, &s->mu, gpr_inf_future);
  }
  gpr_mu_unlock(&s->mu);

  /* delete ALL the things */
  for (i = 0; i < s->nports; i++) {
    server_port *sp = &s->ports[i];
    if (sp->addr.sockaddr.sa_family == AF_UNIX) {
      unlink_if_unix_domain_socket(&sp->addr.un);
    }
    grpc_fd_orphan(sp->emfd, NULL, NULL);
  }
  gpr_free(s->ports);
  gpr_free(s);
}

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

static int get_max_accept_queue_size(void) {
  gpr_once_init(&s_init_max_accept_queue_size, init_max_accept_queue_size);
  return s_max_accept_queue_size;
}

/* Prepare a recently-created socket for listening. */
static int prepare_socket(int fd, const struct sockaddr *addr, int addr_len) {
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;

  if (fd < 0) {
    goto error;
  }

  if (!grpc_set_socket_nonblocking(fd, 1) || !grpc_set_socket_cloexec(fd, 1) ||
      (addr->sa_family != AF_UNIX && (!grpc_set_socket_low_latency(fd, 1) ||
                                      !grpc_set_socket_reuse_addr(fd, 1)))) {
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

  sockname_len = sizeof(sockname_temp);
  if (getsockname(fd, (struct sockaddr *)&sockname_temp, &sockname_len) < 0) {
    goto error;
  }

  return grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);

error:
  if (fd >= 0) {
    close(fd);
  }
  return -1;
}

/* event manager callback when reads are ready */
static void on_read(void *arg, int success) {
  server_port *sp = arg;

  if (!success) {
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
          grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
          return;
        default:
          gpr_log(GPR_ERROR, "Failed accept4: %s", strerror(errno));
          goto error;
      }
    }

    sp->server->cb(
        sp->server->cb_arg,
        grpc_tcp_create(grpc_fd_create(fd), GRPC_TCP_DEFAULT_READ_SLICE_SIZE));
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
  int port;

  port = prepare_socket(fd, addr, addr_len);
  if (port >= 0) {
    gpr_mu_lock(&s->mu);
    GPR_ASSERT(!s->cb && "must add ports before starting server");
    /* append it to the list under a lock */
    if (s->nports == s->port_capacity) {
      s->port_capacity *= 2;
      s->ports = gpr_realloc(s->ports, sizeof(server_port) * s->port_capacity);
    }
    sp = &s->ports[s->nports++];
    sp->server = s;
    sp->fd = fd;
    sp->emfd = grpc_fd_create(fd);
    memcpy(sp->addr.untyped, addr, addr_len);
    sp->addr_len = addr_len;
    GPR_ASSERT(sp->emfd);
    gpr_mu_unlock(&s->mu);
  }

  return port;
}

int grpc_tcp_server_add_port(grpc_tcp_server *s, const void *addr,
                             int addr_len) {
  int allocated_port1 = -1;
  int allocated_port2 = -1;
  unsigned i;
  int fd;
  grpc_dualstack_mode dsmode;
  struct sockaddr_in6 addr6_v4mapped;
  struct sockaddr_in wild4;
  struct sockaddr_in6 wild6;
  struct sockaddr_in addr4_copy;
  struct sockaddr *allocated_addr = NULL;
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;
  int port;

  if (((struct sockaddr *)addr)->sa_family == AF_UNIX) {
    unlink_if_unix_domain_socket(addr);
  }

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (i = 0; i < s->nports; i++) {
      sockname_len = sizeof(sockname_temp);
      if (0 == getsockname(s->ports[i].fd, (struct sockaddr *)&sockname_temp,
                           &sockname_len)) {
        port = grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);
        if (port > 0) {
          allocated_addr = malloc(addr_len);
          memcpy(allocated_addr, addr, addr_len);
          grpc_sockaddr_set_port(allocated_addr, port);
          addr = allocated_addr;
          break;
        }
      }
    }
  }

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
    allocated_port1 = add_socket_to_server(s, fd, addr, addr_len);
    if (fd >= 0 && dsmode == GRPC_DSMODE_DUALSTACK) {
      goto done;
    }

    /* If we didn't get a dualstack socket, also listen on 0.0.0.0. */
    if (port == 0 && allocated_port1 > 0) {
      grpc_sockaddr_set_port((struct sockaddr *)&wild4, allocated_port1);
    }
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
  allocated_port2 = add_socket_to_server(s, fd, addr, addr_len);

done:
  gpr_free(allocated_addr);
  return allocated_port1 >= 0 ? allocated_port1 : allocated_port2;
}

int grpc_tcp_server_get_fd(grpc_tcp_server *s, unsigned index) {
  return (index < s->nports) ? s->ports[index].fd : -1;
}

void grpc_tcp_server_start(grpc_tcp_server *s, grpc_pollset **pollsets,
                           size_t pollset_count, grpc_tcp_server_cb cb,
                           void *cb_arg) {
  size_t i, j;
  GPR_ASSERT(cb);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->cb);
  GPR_ASSERT(s->active_ports == 0);
  s->cb = cb;
  s->cb_arg = cb_arg;
  for (i = 0; i < s->nports; i++) {
    for (j = 0; j < pollset_count; j++) {
      grpc_pollset_add_fd(pollsets[j], s->ports[i].emfd);
    }
    s->ports[i].read_closure.cb = on_read;
    s->ports[i].read_closure.cb_arg = &s->ports[i];
    grpc_fd_notify_on_read(s->ports[i].emfd, &s->ports[i].read_closure);
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);
}

#endif
