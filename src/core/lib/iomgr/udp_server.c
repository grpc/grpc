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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_NEED_UDP
#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/udp_server.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/support/string.h"

#define INIT_PORT_CAP 2

/* one listening port */
typedef struct {
  int fd;
  grpc_fd *emfd;
  grpc_udp_server *server;
  union {
    uint8_t untyped[GRPC_MAX_SOCKADDR_SIZE];
    struct sockaddr sockaddr;
  } addr;
  size_t addr_len;
  grpc_closure read_closure;
  grpc_closure destroyed_closure;
  grpc_udp_server_read_cb read_cb;
  grpc_udp_server_orphan_cb orphan_cb;
} server_port;

/* the overall server */
struct grpc_udp_server {
  gpr_mu mu;
  gpr_cv cv;

  /* active port count: how many ports are actually still listening */
  size_t active_ports;
  /* destroyed port count: how many ports are completely destroyed */
  size_t destroyed_ports;

  /* is this server shutting down? (boolean) */
  int shutdown;

  /* all listening ports */
  server_port *ports;
  size_t nports;
  size_t port_capacity;

  /* shutdown callback */
  grpc_closure *shutdown_complete;

  /* all pollsets interested in new connections */
  grpc_pollset **pollsets;
  /* number of pollsets in the pollsets array */
  size_t pollset_count;
  /* The parent grpc server */
  grpc_server *grpc_server;
};

grpc_udp_server *grpc_udp_server_create(void) {
  grpc_udp_server *s = gpr_malloc(sizeof(grpc_udp_server));
  gpr_mu_init(&s->mu);
  gpr_cv_init(&s->cv);
  s->active_ports = 0;
  s->destroyed_ports = 0;
  s->shutdown = 0;
  s->ports = gpr_malloc(sizeof(server_port) * INIT_PORT_CAP);
  s->nports = 0;
  s->port_capacity = INIT_PORT_CAP;

  return s;
}

static void finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_udp_server *s) {
  grpc_exec_ctx_sched(exec_ctx, s->shutdown_complete, GRPC_ERROR_NONE, NULL);

  gpr_mu_destroy(&s->mu);
  gpr_cv_destroy(&s->cv);

  gpr_free(s->ports);
  gpr_free(s);
}

static void destroyed_port(grpc_exec_ctx *exec_ctx, void *server,
                           grpc_error *error) {
  grpc_udp_server *s = server;
  gpr_mu_lock(&s->mu);
  s->destroyed_ports++;
  if (s->destroyed_ports == s->nports) {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(exec_ctx, s);
  } else {
    gpr_mu_unlock(&s->mu);
  }
}

/* called when all listening endpoints have been shutdown, so no further
   events will be received on them - at this point it's safe to destroy
   things */
static void deactivated_all_ports(grpc_exec_ctx *exec_ctx, grpc_udp_server *s) {
  size_t i;

  /* delete ALL the things */
  gpr_mu_lock(&s->mu);

  if (!s->shutdown) {
    gpr_mu_unlock(&s->mu);
    return;
  }

  if (s->nports) {
    for (i = 0; i < s->nports; i++) {
      server_port *sp = &s->ports[i];
      sp->destroyed_closure.cb = destroyed_port;
      sp->destroyed_closure.cb_arg = s;

      GPR_ASSERT(sp->orphan_cb);
      sp->orphan_cb(sp->emfd);

      grpc_fd_orphan(exec_ctx, sp->emfd, &sp->destroyed_closure, NULL,
                     "udp_listener_shutdown");
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(exec_ctx, s);
  }
}

void grpc_udp_server_destroy(grpc_exec_ctx *exec_ctx, grpc_udp_server *s,
                             grpc_closure *on_done) {
  size_t i;
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(!s->shutdown);
  s->shutdown = 1;

  s->shutdown_complete = on_done;

  /* shutdown all fd's */
  if (s->active_ports) {
    for (i = 0; i < s->nports; i++) {
      grpc_fd_shutdown(exec_ctx, s->ports[i].emfd);
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    deactivated_all_ports(exec_ctx, s);
  }
}

/* Prepare a recently-created socket for listening. */
static int prepare_socket(int fd, const struct sockaddr *addr,
                          size_t addr_len) {
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;
  /* Set send/receive socket buffers to 1 MB */
  int buffer_size_bytes = 1024 * 1024;

  if (fd < 0) {
    goto error;
  }

  if (grpc_set_socket_nonblocking(fd, 1) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Unable to set nonblocking %d: %s", fd, strerror(errno));
    goto error;
  }
  if (grpc_set_socket_cloexec(fd, 1) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Unable to set cloexec %d: %s", fd, strerror(errno));
    goto error;
  }

  if (grpc_set_socket_ip_pktinfo_if_possible(fd) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Unable to set ip_pktinfo.");
    goto error;
  } else if (addr->sa_family == AF_INET6) {
    if (grpc_set_socket_ipv6_recvpktinfo_if_possible(fd) != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Unable to set ipv6_recvpktinfo.");
      goto error;
    }
  }

  GPR_ASSERT(addr_len < ~(socklen_t)0);
  if (bind(fd, addr, (socklen_t)addr_len) < 0) {
    char *addr_str;
    grpc_sockaddr_to_string(&addr_str, addr, 0);
    gpr_log(GPR_ERROR, "bind addr=%s: %s", addr_str, strerror(errno));
    gpr_free(addr_str);
    goto error;
  }

  sockname_len = sizeof(sockname_temp);
  if (getsockname(fd, (struct sockaddr *)&sockname_temp, &sockname_len) < 0) {
    goto error;
  }

  if (grpc_set_socket_sndbuf(fd, buffer_size_bytes) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Failed to set send buffer size to %d bytes",
            buffer_size_bytes);
    goto error;
  }

  if (grpc_set_socket_rcvbuf(fd, buffer_size_bytes) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Failed to set receive buffer size to %d bytes",
            buffer_size_bytes);
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
static void on_read(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  server_port *sp = arg;

  if (error != GRPC_ERROR_NONE) {
    gpr_mu_lock(&sp->server->mu);
    if (0 == --sp->server->active_ports) {
      gpr_mu_unlock(&sp->server->mu);
      deactivated_all_ports(exec_ctx, sp->server);
    } else {
      gpr_mu_unlock(&sp->server->mu);
    }
    return;
  }

  /* Tell the registered callback that data is available to read. */
  GPR_ASSERT(sp->read_cb);
  sp->read_cb(exec_ctx, sp->emfd, sp->server->grpc_server);

  /* Re-arm the notification event so we get another chance to read. */
  grpc_fd_notify_on_read(exec_ctx, sp->emfd, &sp->read_closure);
}

static int add_socket_to_server(grpc_udp_server *s, int fd,
                                const struct sockaddr *addr, size_t addr_len,
                                grpc_udp_server_read_cb read_cb,
                                grpc_udp_server_orphan_cb orphan_cb) {
  server_port *sp;
  int port;
  char *addr_str;
  char *name;

  port = prepare_socket(fd, addr, addr_len);
  if (port >= 0) {
    grpc_sockaddr_to_string(&addr_str, (struct sockaddr *)&addr, 1);
    gpr_asprintf(&name, "udp-server-listener:%s", addr_str);
    gpr_free(addr_str);
    gpr_mu_lock(&s->mu);
    /* append it to the list under a lock */
    if (s->nports == s->port_capacity) {
      s->port_capacity *= 2;
      s->ports = gpr_realloc(s->ports, sizeof(server_port) * s->port_capacity);
    }
    sp = &s->ports[s->nports++];
    sp->server = s;
    sp->fd = fd;
    sp->emfd = grpc_fd_create(fd, name);
    memcpy(sp->addr.untyped, addr, addr_len);
    sp->addr_len = addr_len;
    sp->read_cb = read_cb;
    sp->orphan_cb = orphan_cb;
    GPR_ASSERT(sp->emfd);
    gpr_mu_unlock(&s->mu);
    gpr_free(name);
  }

  return port;
}

int grpc_udp_server_add_port(grpc_udp_server *s, const void *addr,
                             size_t addr_len, grpc_udp_server_read_cb read_cb,
                             grpc_udp_server_orphan_cb orphan_cb) {
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

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (i = 0; i < s->nports; i++) {
      sockname_len = sizeof(sockname_temp);
      if (0 == getsockname(s->ports[i].fd, (struct sockaddr *)&sockname_temp,
                           &sockname_len)) {
        port = grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);
        if (port > 0) {
          allocated_addr = gpr_malloc(addr_len);
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
    // TODO(rjshade): Test and propagate the returned grpc_error*:
    grpc_create_dualstack_socket(addr, SOCK_DGRAM, IPPROTO_UDP, &dsmode, &fd);
    allocated_port1 =
        add_socket_to_server(s, fd, addr, addr_len, read_cb, orphan_cb);
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

  // TODO(rjshade): Test and propagate the returned grpc_error*:
  grpc_create_dualstack_socket(addr, SOCK_DGRAM, IPPROTO_UDP, &dsmode, &fd);
  if (fd < 0) {
    gpr_log(GPR_ERROR, "Unable to create socket: %s", strerror(errno));
  }
  if (dsmode == GRPC_DSMODE_IPV4 &&
      grpc_sockaddr_is_v4mapped(addr, &addr4_copy)) {
    addr = (struct sockaddr *)&addr4_copy;
    addr_len = sizeof(addr4_copy);
  }
  allocated_port2 =
      add_socket_to_server(s, fd, addr, addr_len, read_cb, orphan_cb);

done:
  gpr_free(allocated_addr);
  return allocated_port1 >= 0 ? allocated_port1 : allocated_port2;
}

int grpc_udp_server_get_fd(grpc_udp_server *s, unsigned port_index) {
  return (port_index < s->nports) ? s->ports[port_index].fd : -1;
}

void grpc_udp_server_start(grpc_exec_ctx *exec_ctx, grpc_udp_server *s,
                           grpc_pollset **pollsets, size_t pollset_count,
                           grpc_server *server) {
  size_t i, j;
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(s->active_ports == 0);
  s->pollsets = pollsets;
  s->grpc_server = server;
  for (i = 0; i < s->nports; i++) {
    for (j = 0; j < pollset_count; j++) {
      grpc_pollset_add_fd(exec_ctx, pollsets[j], s->ports[i].emfd);
    }
    s->ports[i].read_closure.cb = on_read;
    s->ports[i].read_closure.cb_arg = &s->ports[i];
    grpc_fd_notify_on_read(exec_ctx, s->ports[i].emfd,
                           &s->ports[i].read_closure);
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);
}

#endif
#endif
