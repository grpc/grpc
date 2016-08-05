/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/support/port_platform.h>
#ifdef GPR_POSIX_SOCKET

#include "src/core/ext/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#include <sys/types.h>

#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/string.h"

#include <arpa/inet.h>

typedef struct fd_pair {
  grpc_fd *grpc_fd;
  int fd;
  struct fd_pair *next;
} fd_pair;

typedef struct {
  int id;
  ares_socket_t socks[ARES_GETSOCK_MAXNUM];
  int bitmask;
  grpc_closure driver_closure;
  grpc_pollset_set *pollset_set;
  ares_channel *channel;
  fd_pair *fds;
} driver;

struct grpc_ares_request {
  char *name;
  char *host;
  char *port;
  char *default_port;
  grpc_polling_entity *pollent;
  grpc_closure *on_done;
  grpc_resolved_addresses **addrs_out;
  grpc_closure request_closure;
  void *arg;
  ares_channel channel;
  driver ev_driver;
};

struct grpc_pollset_set {
  gpr_mu mu;

  size_t pollset_count;
  size_t pollset_capacity;
  grpc_pollset **pollsets;

  size_t pollset_set_count;
  size_t pollset_set_capacity;
  struct grpc_pollset_set **pollset_sets;

  size_t fd_count;
  size_t fd_capacity;
  grpc_fd **fds;
};

static void driver_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);

static fd_pair *get_fd(fd_pair **head, int fd) {
  fd_pair dummy_head;
  fd_pair *node;
  fd_pair *ret;
  dummy_head.next = *head;
  node = &dummy_head;
  while (node->next != NULL) {
    if (node->next->fd == fd) {
      ret = node->next;
      node->next = node->next->next;
      *head = dummy_head.next;
      return ret;
    }
  }
  return NULL;
}

static void notify_on_event(grpc_exec_ctx *exec_ctx, driver *ev_driver) {
  size_t i;
  fd_pair *new_list = NULL;
  ev_driver->bitmask =
      ares_getsock(*ev_driver->channel, ev_driver->socks, ARES_GETSOCK_MAXNUM);
  grpc_closure_init(&ev_driver->driver_closure, driver_cb, ev_driver);
  for (i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    char *final_name;
    gpr_asprintf(&final_name, "host1%" PRIuPTR, i);

    if (ARES_GETSOCK_READABLE(ev_driver->bitmask, i) ||
        ARES_GETSOCK_WRITABLE(ev_driver->bitmask, i)) {
      gpr_log(GPR_ERROR, "%d", ev_driver->socks[i]);
      fd_pair *fdp = get_fd(&ev_driver->fds, ev_driver->socks[i]);
      if (!fdp) {
        gpr_log(GPR_ERROR, "new fd");
        fdp = gpr_malloc(sizeof(fd_pair));
        fdp->grpc_fd = grpc_fd_create(ev_driver->socks[i], final_name);
        fdp->fd = ev_driver->socks[i];
        grpc_pollset_set_add_fd(exec_ctx, ev_driver->pollset_set, fdp->grpc_fd);
        // new_fd_pair->grpc_fd = fd;
        // new_fd_pair->next = ev_driver->fds;
      }
      fdp->next = new_list;
      new_list = fdp;

      if (ARES_GETSOCK_READABLE(ev_driver->bitmask, i)) {
        gpr_log(GPR_ERROR, "READABLE");

        grpc_fd_notify_on_read(exec_ctx, fdp->grpc_fd,
                               &ev_driver->driver_closure);
      }
      if (ARES_GETSOCK_WRITABLE(ev_driver->bitmask, i)) {
        gpr_log(GPR_ERROR, "writable");

        grpc_fd_notify_on_write(exec_ctx, fdp->grpc_fd,
                                &ev_driver->driver_closure);
      }
    }
    gpr_free(final_name);
  }

  while (ev_driver->fds != NULL) {
    fd_pair *cur;
    // int fd;s
    cur = ev_driver->fds;
    ev_driver->fds = ev_driver->fds->next;
    gpr_log(GPR_ERROR, "fd in ev_driver: %d\n", cur->fd);
    grpc_pollset_set_del_fd(exec_ctx, ev_driver->pollset_set, cur->grpc_fd);
    gpr_log(GPR_ERROR, "grpc_pollset_set_del_fd");
    grpc_fd_shutdown(exec_ctx, cur->grpc_fd);
    gpr_log(GPR_ERROR, "grpc_fd_shutdown");
    grpc_fd_orphan(exec_ctx, cur->grpc_fd, NULL, NULL, "come on..");
    gpr_log(GPR_ERROR, "grpc_fd_orphan");
    gpr_free(cur);
  }

  ev_driver->fds = new_list;

  gpr_log(GPR_ERROR, "eof notify_on_event");
}

static void driver_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  driver *d = arg;
  size_t i;
  gpr_log(GPR_ERROR, "driver_cb");
  if (error == GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "GRPC_ERROR_NONE");
    for (i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      ares_process_fd(
          *d->channel,
          ARES_GETSOCK_READABLE(d->bitmask, i) ? d->socks[i] : ARES_SOCKET_BAD,
          ARES_GETSOCK_WRITABLE(d->bitmask, i) ? d->socks[i] : ARES_SOCKET_BAD);
    }
  }
  notify_on_event(exec_ctx, d);
  grpc_exec_ctx_flush(exec_ctx);
}

static void on_done_cb(void *arg, int status, int timeouts,
                       struct hostent *hostent) {
  gpr_log(GPR_ERROR, "status: %d", status);
  grpc_ares_request *r = (grpc_ares_request *)arg;
  grpc_error *err;
  gpr_log(GPR_ERROR, "status: %s", r->name);
  grpc_resolved_addresses **addresses = r->addrs_out;
  size_t i;

  if (status == ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "status ARES_SUCCESS");
    err = GRPC_ERROR_NONE;
    *addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
    for ((*addresses)->naddrs = 0;
         hostent->h_addr_list[(*addresses)->naddrs] != NULL;
         (*addresses)->naddrs++) {
    }
    gpr_log(GPR_ERROR, "naddr: %" PRIuPTR, (*addresses)->naddrs);
    (*addresses)->addrs =
        gpr_malloc(sizeof(grpc_resolved_address) * (*addresses)->naddrs);
    for (i = 0; i < (*addresses)->naddrs; i++) {
      if (hostent->h_addrtype == AF_INET6) {
        char output[INET6_ADDRSTRLEN];
        gpr_log(GPR_ERROR, "AF_INET6");
        struct sockaddr_in6 *addr;

        (*addresses)->addrs[i].len = sizeof(struct sockaddr_in6);
        // &(*addresses)->addrs[i].addr =
        // gpr_malloc((*addresses)->addrs[i].len);
        addr = (struct sockaddr_in6 *)&(*addresses)->addrs[i].addr;

        memcpy(&addr->sin6_addr, hostent->h_addr_list[i],
               sizeof(struct in6_addr));
        ares_inet_ntop(AF_INET6, &addr->sin6_addr, output, INET6_ADDRSTRLEN);
        gpr_log(GPR_ERROR, "addr: %s", output);
        gpr_log(GPR_ERROR, "port: %s", r->port);
        addr->sin6_family = (sa_family_t)hostent->h_addrtype;
        addr->sin6_port = htons(atoi(r->port));  // TODO: port
      } else {
        gpr_log(GPR_ERROR, "AF_INET");
        struct sockaddr_in *addr;
        (*addresses)->addrs[i].len = sizeof(struct sockaddr_in);
        // &(*addresses)->addrs[i].addr =
        // gpr_malloc((*addresses)->addrs[i].len);
        addr = (struct sockaddr_in *)&(*addresses)->addrs[i].addr;
        memcpy(&addr->sin_addr, hostent->h_addr_list[i],
               sizeof(struct in_addr));
        addr->sin_family = (sa_family_t)hostent->h_addrtype;
        addr->sin_port = htons(atoi(r->port));  // TODO: port
      }
    }
    // ares_destroy(r->channel);
  } else {
    gpr_log(GPR_ERROR, "status not ARES_SUCCESS");
    err = grpc_error_set_str(
        grpc_error_set_str(
            grpc_error_set_str(grpc_error_set_int(GRPC_ERROR_CREATE("OS Error"),
                                                  GRPC_ERROR_INT_ERRNO, status),
                               GRPC_ERROR_STR_OS_ERROR, gai_strerror(status)),
            GRPC_ERROR_STR_SYSCALL, "getaddrinfo"),
        GRPC_ERROR_STR_TARGET_ADDRESS, r->name);
  }
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_exec_ctx_sched(&exec_ctx, r->on_done, err, NULL);
  grpc_exec_ctx_flush(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void resolve_address_impl(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_error *error) {
  int status;

  grpc_ares_request *r = (grpc_ares_request *)arg;
  gpr_log(GPR_ERROR, "Really?");

  status = ares_init(&r->channel);
  if (status != ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "ares_init failed");
  }

  driver *ev_driver = &r->ev_driver;
  ev_driver->channel = &r->channel;
  gpr_log(GPR_ERROR, "before ares_gethostbyname %s", r->host);
  ares_gethostbyname(r->channel, r->host, AF_UNSPEC, on_done_cb, r);
  gpr_log(GPR_ERROR, "before ares_getsock");
  notify_on_event(exec_ctx, &r->ev_driver);

  gpr_log(GPR_ERROR, "before poll");

  gpr_log(GPR_ERROR, "eof resolve_address_impl");
}

static int try_fake_resolve(const char *name, const char *port,
                            grpc_resolved_addresses **addresses) {
  struct sockaddr_in sa;
  struct sockaddr_in6 sa6;
  if (0 != ares_inet_pton(AF_INET, name, &(sa.sin_addr))) {
    gpr_log(GPR_ERROR, "AF_INET");
    *addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
    (*addresses)->naddrs = 1;
    (*addresses)->addrs =
        gpr_malloc(sizeof(grpc_resolved_address) * (*addresses)->naddrs);
    (*addresses)->addrs[0].len = sizeof(struct sockaddr_in);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(port));  // TODO: port
    memcpy(&(*addresses)->addrs[0].addr, &sa, sizeof(struct sockaddr_in));
    return 1;
  }
  if (0 != ares_inet_pton(AF_INET6, name, &(sa6.sin6_addr))) {
    char output[INET6_ADDRSTRLEN];
    gpr_log(GPR_ERROR, "AF_INET6");
    *addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
    (*addresses)->naddrs = 1;
    (*addresses)->addrs =
        gpr_malloc(sizeof(grpc_resolved_address) * (*addresses)->naddrs);
    (*addresses)->addrs[0].len = sizeof(struct sockaddr_in6);
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(atoi(port));  // TODO: port
    memcpy(&(*addresses)->addrs[0].addr, &sa6, sizeof(struct sockaddr_in6));
    ares_inet_ntop(AF_INET6, &sa6.sin6_addr, output, INET6_ADDRSTRLEN);
    gpr_log(GPR_ERROR, "addr: %s", output);
    gpr_log(GPR_ERROR, "port: %s", port);

    return 1;
  }
  return 0;
}

grpc_ares_request *grpc_resolve_address_ares(grpc_exec_ctx *exec_ctx,
                                             const char *name,
                                             const char *default_port,
                                             grpc_pollset_set *pollset_set,
                                             grpc_closure *on_done,
                                             grpc_resolved_addresses **addrs) {
  char *host;
  char *port;
  grpc_error *err;

  grpc_ares_request *r = gpr_malloc(sizeof(grpc_ares_request));
  r->name = gpr_strdup(name);
  r->default_port = gpr_strdup(default_port);
  r->on_done = on_done;
  r->addrs_out = addrs;
  r->ev_driver.pollset_set = pollset_set;
  r->ev_driver.fds = NULL;

  if (name[0] == 'u' && name[1] == 'n' && name[2] == 'i' && name[3] == 'x' &&
      name[4] == ':' && name[5] != 0) {
    grpc_resolve_unix_domain_address(name + 5, addrs);
  }

  /* parse name, splitting it into host and port parts */
  gpr_split_host_port(name, &host, &port);
  if (host == NULL) {
    err = grpc_error_set_str(GRPC_ERROR_CREATE("unparseable host:port"),
                             GRPC_ERROR_STR_TARGET_ADDRESS, name);
    grpc_exec_ctx_sched(exec_ctx, on_done, err, NULL);
  } else if (port == NULL) {
    if (default_port == NULL) {
      err = grpc_error_set_str(GRPC_ERROR_CREATE("no port in name"),
                               GRPC_ERROR_STR_TARGET_ADDRESS, name);
      grpc_exec_ctx_sched(exec_ctx, on_done, err, NULL);
    }
    port = gpr_strdup(default_port);
  } else if (try_fake_resolve(host, port, addrs)) {
    grpc_exec_ctx_sched(exec_ctx, on_done, GRPC_ERROR_NONE, NULL);
  } else {
    r->port = gpr_strdup(port);
    r->host = gpr_strdup(host);
    grpc_closure_init(&r->request_closure, resolve_address_impl, r);
    grpc_exec_ctx_sched(exec_ctx, &r->request_closure, GRPC_ERROR_NONE, NULL);
  }

  gpr_free(host);
  gpr_free(port);
  return r;
}

void grpc_ares_init(void) {
  int status = ares_library_init(ARES_LIB_INIT_ALL);
  if (status != ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "ares_library_init failed");
  }
}

#endif
