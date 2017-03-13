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

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/tcp_server.h"

#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/string.h"

#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

static gpr_once s_init_max_accept_queue_size;
static int s_max_accept_queue_size;

/* one listening port */
typedef struct grpc_tcp_listener grpc_tcp_listener;
struct grpc_tcp_listener {
  int fd;
  grpc_fd *emfd;
  grpc_tcp_server *server;
  grpc_resolved_address addr;
  int port;
  unsigned port_index;
  unsigned fd_index;
  grpc_closure read_closure;
  grpc_closure destroyed_closure;
  struct grpc_tcp_listener *next;
  /* sibling is a linked list of all listeners for a given port. add_port and
     clone_port place all new listeners in the same sibling list. A member of
     the 'sibling' list is also a member of the 'next' list. The head of each
     sibling list has is_sibling==0, and subsequent members of sibling lists
     have is_sibling==1. is_sibling allows separate sibling lists to be
     identified while iterating through 'next'. */
  struct grpc_tcp_listener *sibling;
  int is_sibling;
};

/* the overall server */
struct grpc_tcp_server {
  gpr_refcount refs;
  /* Called whenever accept() succeeds on a server port. */
  grpc_tcp_server_cb on_accept_cb;
  void *on_accept_cb_arg;

  gpr_mu mu;

  /* active port count: how many ports are actually still listening */
  size_t active_ports;
  /* destroyed port count: how many ports are completely destroyed */
  size_t destroyed_ports;

  /* is this server shutting down? */
  bool shutdown;
  /* have listeners been shutdown? */
  bool shutdown_listeners;
  /* use SO_REUSEPORT */
  bool so_reuseport;
  /* expand wildcard addresses to a list of all local addresses */
  bool expand_wildcard_addrs;

  /* linked list of server ports */
  grpc_tcp_listener *head;
  grpc_tcp_listener *tail;
  unsigned nports;

  /* List of closures passed to shutdown_starting_add(). */
  grpc_closure_list shutdown_starting;

  /* shutdown callback */
  grpc_closure *shutdown_complete;

  /* all pollsets interested in new connections */
  grpc_pollset **pollsets;
  /* number of pollsets in the pollsets array */
  size_t pollset_count;

  /* next pollset to assign a channel to */
  gpr_atm next_pollset_to_assign;

  grpc_resource_quota *resource_quota;
};

static gpr_once check_init = GPR_ONCE_INIT;
static bool has_so_reuseport = false;

static void init(void) {
#ifndef GPR_MANYLINUX1
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s >= 0) {
    has_so_reuseport = GRPC_LOG_IF_ERROR("check for SO_REUSEPORT",
                                         grpc_set_socket_reuse_port(s, 1));
    close(s);
  }
#endif
}

grpc_error *grpc_tcp_server_create(grpc_exec_ctx *exec_ctx,
                                   grpc_closure *shutdown_complete,
                                   const grpc_channel_args *args,
                                   grpc_tcp_server **server) {
  gpr_once_init(&check_init, init);

  grpc_tcp_server *s = gpr_zalloc(sizeof(grpc_tcp_server));
  s->so_reuseport = has_so_reuseport;
  s->resource_quota = grpc_resource_quota_create(NULL);
  s->expand_wildcard_addrs = false;
  for (size_t i = 0; i < (args == NULL ? 0 : args->num_args); i++) {
    if (0 == strcmp(GRPC_ARG_ALLOW_REUSEPORT, args->args[i].key)) {
      if (args->args[i].type == GRPC_ARG_INTEGER) {
        s->so_reuseport =
            has_so_reuseport && (args->args[i].value.integer != 0);
      } else {
        grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);
        gpr_free(s);
        return GRPC_ERROR_CREATE(GRPC_ARG_ALLOW_REUSEPORT
                                 " must be an integer");
      }
    } else if (0 == strcmp(GRPC_ARG_RESOURCE_QUOTA, args->args[i].key)) {
      if (args->args[i].type == GRPC_ARG_POINTER) {
        grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);
        s->resource_quota =
            grpc_resource_quota_ref_internal(args->args[i].value.pointer.p);
      } else {
        grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);
        gpr_free(s);
        return GRPC_ERROR_CREATE(GRPC_ARG_RESOURCE_QUOTA
                                 " must be a pointer to a buffer pool");
      }
    } else if (0 == strcmp(GRPC_ARG_EXPAND_WILDCARD_ADDRS, args->args[i].key)) {
      if (args->args[i].type == GRPC_ARG_INTEGER) {
        s->expand_wildcard_addrs = (args->args[i].value.integer != 0);
      } else {
        grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);
        gpr_free(s);
        return GRPC_ERROR_CREATE(GRPC_ARG_EXPAND_WILDCARD_ADDRS
                                 " must be an integer");
      }
    }
  }
  gpr_ref_init(&s->refs, 1);
  gpr_mu_init(&s->mu);
  s->active_ports = 0;
  s->destroyed_ports = 0;
  s->shutdown = false;
  s->shutdown_starting.head = NULL;
  s->shutdown_starting.tail = NULL;
  s->shutdown_complete = shutdown_complete;
  s->on_accept_cb = NULL;
  s->on_accept_cb_arg = NULL;
  s->head = NULL;
  s->tail = NULL;
  s->nports = 0;
  gpr_atm_no_barrier_store(&s->next_pollset_to_assign, 0);
  *server = s;
  return GRPC_ERROR_NONE;
}

static void finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(s->shutdown);
  gpr_mu_unlock(&s->mu);
  if (s->shutdown_complete != NULL) {
    grpc_closure_sched(exec_ctx, s->shutdown_complete, GRPC_ERROR_NONE);
  }

  gpr_mu_destroy(&s->mu);

  while (s->head) {
    grpc_tcp_listener *sp = s->head;
    s->head = sp->next;
    gpr_free(sp);
  }

  grpc_resource_quota_unref_internal(exec_ctx, s->resource_quota);

  gpr_free(s);
}

static void destroyed_port(grpc_exec_ctx *exec_ctx, void *server,
                           grpc_error *error) {
  grpc_tcp_server *s = server;
  gpr_mu_lock(&s->mu);
  s->destroyed_ports++;
  if (s->destroyed_ports == s->nports) {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(exec_ctx, s);
  } else {
    GPR_ASSERT(s->destroyed_ports < s->nports);
    gpr_mu_unlock(&s->mu);
  }
}

/* called when all listening endpoints have been shutdown, so no further
   events will be received on them - at this point it's safe to destroy
   things */
static void deactivated_all_ports(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  /* delete ALL the things */
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(s->shutdown);

  if (s->head) {
    grpc_tcp_listener *sp;
    for (sp = s->head; sp; sp = sp->next) {
      grpc_unlink_if_unix_domain_socket(&sp->addr);
      grpc_closure_init(&sp->destroyed_closure, destroyed_port, s,
                        grpc_schedule_on_exec_ctx);
      grpc_fd_orphan(exec_ctx, sp->emfd, &sp->destroyed_closure, NULL,
                     "tcp_listener_shutdown");
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(exec_ctx, s);
  }
}

static void tcp_server_destroy(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(!s->shutdown);
  s->shutdown = true;

  /* shutdown all fd's */
  if (s->active_ports) {
    grpc_tcp_listener *sp;
    for (sp = s->head; sp; sp = sp->next) {
      grpc_fd_shutdown(exec_ctx, sp->emfd,
                       GRPC_ERROR_CREATE("Server destroyed"));
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    deactivated_all_ports(exec_ctx, s);
  }
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

/* Prepare a recently-created socket for listening. */
static grpc_error *prepare_socket(int fd, const grpc_resolved_address *addr,
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
  grpc_error *ret = grpc_error_set_int(
      GRPC_ERROR_CREATE_REFERENCING("Unable to configure socket", &err, 1),
      GRPC_ERROR_INT_FD, fd);
  GRPC_ERROR_UNREF(err);
  return ret;
}

/* event manager callback when reads are ready */
static void on_read(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *err) {
  grpc_tcp_listener *sp = arg;

  if (err != GRPC_ERROR_NONE) {
    goto error;
  }

  grpc_pollset *read_notifier_pollset =
      sp->server->pollsets[(size_t)gpr_atm_no_barrier_fetch_add(
                               &sp->server->next_pollset_to_assign, 1) %
                           sp->server->pollset_count];

  /* loop until accept4 returns EAGAIN, and then re-arm notification */
  for (;;) {
    grpc_resolved_address addr;
    char *addr_str;
    char *name;
    addr.len = sizeof(struct sockaddr_storage);
    /* Note: If we ever decide to return this address to the user, remember to
       strip off the ::ffff:0.0.0.0/96 prefix first. */
    int fd = grpc_accept4(sp->fd, &addr, 1, 1);
    if (fd < 0) {
      switch (errno) {
        case EINTR:
          continue;
        case EAGAIN:
          grpc_fd_notify_on_read(exec_ctx, sp->emfd, &sp->read_closure);
          return;
        default:
          gpr_mu_lock(&sp->server->mu);
          if (!sp->server->shutdown_listeners) {
            gpr_log(GPR_ERROR, "Failed accept4: %s", strerror(errno));
          } else {
            /* if we have shutdown listeners, accept4 could fail, and we
               needn't notify users */
          }
          gpr_mu_unlock(&sp->server->mu);
          goto error;
      }
    }

    grpc_set_socket_no_sigpipe_if_possible(fd);

    addr_str = grpc_sockaddr_to_uri(&addr);
    gpr_asprintf(&name, "tcp-server-connection:%s", addr_str);

    if (grpc_tcp_trace) {
      gpr_log(GPR_DEBUG, "SERVER_CONNECT: incoming connection: %s", addr_str);
    }

    grpc_fd *fdobj = grpc_fd_create(fd, name);

    grpc_pollset_add_fd(exec_ctx, read_notifier_pollset, fdobj);

    // Create acceptor.
    grpc_tcp_server_acceptor *acceptor = gpr_malloc(sizeof(*acceptor));
    acceptor->from_server = sp->server;
    acceptor->port_index = sp->port_index;
    acceptor->fd_index = sp->fd_index;

    sp->server->on_accept_cb(
        exec_ctx, sp->server->on_accept_cb_arg,
        grpc_tcp_create(fdobj, sp->server->resource_quota,
                        GRPC_TCP_DEFAULT_READ_SLICE_SIZE, addr_str),
        read_notifier_pollset, acceptor);

    gpr_free(name);
    gpr_free(addr_str);
  }

  GPR_UNREACHABLE_CODE(return );

error:
  gpr_mu_lock(&sp->server->mu);
  if (0 == --sp->server->active_ports && sp->server->shutdown) {
    gpr_mu_unlock(&sp->server->mu);
    deactivated_all_ports(exec_ctx, sp->server);
  } else {
    gpr_mu_unlock(&sp->server->mu);
  }
}

static grpc_error *add_socket_to_server(grpc_tcp_server *s, int fd,
                                        const grpc_resolved_address *addr,
                                        unsigned port_index, unsigned fd_index,
                                        grpc_tcp_listener **listener) {
  grpc_tcp_listener *sp = NULL;
  int port = -1;
  char *addr_str;
  char *name;

  grpc_error *err = prepare_socket(fd, addr, s->so_reuseport, &port);
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
static grpc_error *add_addr_to_server(grpc_tcp_server *s,
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

/* Bind to "::" to get a port number not used by any address. */
static grpc_error *get_unused_port(int *port) {
  grpc_resolved_address wild;
  grpc_sockaddr_make_wildcard6(0, &wild);
  grpc_dualstack_mode dsmode;
  int fd;
  grpc_error *err =
      grpc_create_dualstack_socket(&wild, SOCK_STREAM, 0, &dsmode, &fd);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }
  if (dsmode == GRPC_DSMODE_IPV4) {
    grpc_sockaddr_make_wildcard4(0, &wild);
  }
  if (bind(fd, (const struct sockaddr *)wild.addr, (socklen_t)wild.len) != 0) {
    err = GRPC_OS_ERROR(errno, "bind");
    close(fd);
    return err;
  }
  if (getsockname(fd, (struct sockaddr *)wild.addr, (socklen_t *)&wild.len) !=
      0) {
    err = GRPC_OS_ERROR(errno, "getsockname");
    close(fd);
    return err;
  }
  close(fd);
  *port = grpc_sockaddr_get_port(&wild);
  return *port <= 0 ? GRPC_ERROR_CREATE("Bad port") : GRPC_ERROR_NONE;
}

/* Return the listener in s with address addr or NULL. */
static grpc_tcp_listener *find_listener_with_addr(grpc_tcp_server *s,
                                                  grpc_resolved_address *addr) {
  grpc_tcp_listener *l;
  gpr_mu_lock(&s->mu);
  for (l = s->head; l != NULL; l = l->next) {
    if (l->addr.len != addr->len) {
      continue;
    }
    if (memcmp(l->addr.addr, addr->addr, addr->len) == 0) {
      break;
    }
  }
  gpr_mu_unlock(&s->mu);
  return l;
}

/* Get all addresses assigned to network interfaces on the machine and create a
   listener for each. requested_port is the port to use for every listener, or 0
   to select one random port that will be used for every listener. Set *out_port
   to the port selected. Return GRPC_ERROR_NONE only if all listeners were
   added. */
static grpc_error *add_all_local_addrs_to_server(grpc_tcp_server *s,
                                                 unsigned port_index,
                                                 int requested_port,
                                                 int *out_port) {
  struct ifaddrs *ifa = NULL;
  struct ifaddrs *ifa_it;
  unsigned fd_index = 0;
  grpc_tcp_listener *sp = NULL;
  grpc_error *err = GRPC_ERROR_NONE;
  if (requested_port == 0) {
    /* Note: There could be a race where some local addrs can listen on the
       selected port and some can't. The sane way to handle this would be to
       retry by recreating the whole grpc_tcp_server. Backing out individual
       listeners and orphaning the FDs looks like too much trouble. */
    if ((err = get_unused_port(&requested_port)) != GRPC_ERROR_NONE) {
      return err;
    } else if (requested_port <= 0) {
      return GRPC_ERROR_CREATE("Bad get_unused_port()");
    }
    gpr_log(GPR_DEBUG, "Picked unused port %d", requested_port);
  }
  if (getifaddrs(&ifa) != 0 || ifa == NULL) {
    return GRPC_OS_ERROR(errno, "getifaddrs");
  }
  for (ifa_it = ifa; ifa_it != NULL; ifa_it = ifa_it->ifa_next) {
    grpc_resolved_address addr;
    char *addr_str = NULL;
    grpc_dualstack_mode dsmode;
    grpc_tcp_listener *new_sp = NULL;
    const char *ifa_name = (ifa_it->ifa_name ? ifa_it->ifa_name : "<unknown>");
    if (ifa_it->ifa_addr == NULL) {
      continue;
    } else if (ifa_it->ifa_addr->sa_family == AF_INET) {
      addr.len = sizeof(struct sockaddr_in);
    } else if (ifa_it->ifa_addr->sa_family == AF_INET6) {
      addr.len = sizeof(struct sockaddr_in6);
    } else {
      continue;
    }
    memcpy(addr.addr, ifa_it->ifa_addr, addr.len);
    if (!grpc_sockaddr_set_port(&addr, requested_port)) {
      /* Should never happen, because we check sa_family above. */
      err = GRPC_ERROR_CREATE("Failed to set port");
      break;
    }
    if (grpc_sockaddr_to_string(&addr_str, &addr, 0) < 0) {
      addr_str = gpr_strdup("<error>");
    }
    gpr_log(GPR_DEBUG,
            "Adding local addr from interface %s flags 0x%x to server: %s",
            ifa_name, ifa_it->ifa_flags, addr_str);
    /* We could have multiple interfaces with the same address (e.g., bonding),
       so look for duplicates. */
    if (find_listener_with_addr(s, &addr) != NULL) {
      gpr_log(GPR_DEBUG, "Skipping duplicate addr %s on interface %s", addr_str,
              ifa_name);
      gpr_free(addr_str);
      continue;
    }
    if ((err = add_addr_to_server(s, &addr, port_index, fd_index, &dsmode,
                                  &new_sp)) != GRPC_ERROR_NONE) {
      char *err_str = NULL;
      grpc_error *root_err;
      if (gpr_asprintf(&err_str, "Failed to add listener: %s", addr_str) < 0) {
        err_str = gpr_strdup("Failed to add listener");
      }
      root_err = GRPC_ERROR_CREATE(err_str);
      gpr_free(err_str);
      gpr_free(addr_str);
      err = grpc_error_add_child(root_err, err);
      break;
    } else {
      GPR_ASSERT(requested_port == new_sp->port);
      ++fd_index;
      if (sp != NULL) {
        new_sp->is_sibling = 1;
        sp->sibling = new_sp;
      }
      sp = new_sp;
    }
    gpr_free(addr_str);
  }
  freeifaddrs(ifa);
  if (err != GRPC_ERROR_NONE) {
    return err;
  } else if (sp == NULL) {
    return GRPC_ERROR_CREATE("No local addresses");
  } else {
    *out_port = sp->port;
    return GRPC_ERROR_NONE;
  }
}

/* Treat :: or 0.0.0.0 as a family-agnostic wildcard. */
static grpc_error *add_wildcard_addrs_to_server(grpc_tcp_server *s,
                                                unsigned port_index,
                                                int requested_port,
                                                int *out_port) {
  grpc_resolved_address wild4;
  grpc_resolved_address wild6;
  unsigned fd_index = 0;
  grpc_dualstack_mode dsmode;
  grpc_tcp_listener *sp = NULL;
  grpc_tcp_listener *sp2 = NULL;
  grpc_error *v6_err = GRPC_ERROR_NONE;
  grpc_error *v4_err = GRPC_ERROR_NONE;
  *out_port = -1;
  if (s->expand_wildcard_addrs) {
    return add_all_local_addrs_to_server(s, port_index, requested_port,
                                         out_port);
  }
  grpc_sockaddr_make_wildcards(requested_port, &wild4, &wild6);
  /* Try listening on IPv6 first. */
  if ((v6_err = add_addr_to_server(s, &wild6, port_index, fd_index, &dsmode,
                                   &sp)) == GRPC_ERROR_NONE) {
    ++fd_index;
    requested_port = *out_port = sp->port;
    if (dsmode == GRPC_DSMODE_DUALSTACK || dsmode == GRPC_DSMODE_IPV4) {
      return GRPC_ERROR_NONE;
    }
  }
  /* If we got a v6-only socket or nothing, try adding 0.0.0.0. */
  grpc_sockaddr_set_port(&wild4, requested_port);
  if ((v4_err = add_addr_to_server(s, &wild4, port_index, fd_index, &dsmode,
                                   &sp2)) == GRPC_ERROR_NONE) {
    *out_port = sp2->port;
    if (sp != NULL) {
      sp2->is_sibling = 1;
      sp->sibling = sp2;
    }
  }
  if (*out_port > 0) {
    GRPC_LOG_IF_ERROR("Failed to add :: listener", v6_err);
    GRPC_LOG_IF_ERROR("Failed to add 0.0.0.0 listener", v4_err);
    return GRPC_ERROR_NONE;
  } else {
    grpc_error *root_err =
        GRPC_ERROR_CREATE("Failed to add any wildcard listeners");
    GPR_ASSERT(v6_err != GRPC_ERROR_NONE && v4_err != GRPC_ERROR_NONE);
    root_err = grpc_error_add_child(root_err, v6_err);
    root_err = grpc_error_add_child(root_err, v4_err);
    return root_err;
  }
}

static grpc_error *clone_port(grpc_tcp_listener *listener, unsigned count) {
  grpc_tcp_listener *sp = NULL;
  char *addr_str;
  char *name;
  grpc_error *err;

  for (grpc_tcp_listener *l = listener->next; l && l->is_sibling; l = l->next) {
    l->fd_index += count;
  }

  for (unsigned i = 0; i < count; i++) {
    int fd = -1;
    int port = -1;
    grpc_dualstack_mode dsmode;
    err = grpc_create_dualstack_socket(&listener->addr, SOCK_STREAM, 0, &dsmode,
                                       &fd);
    if (err != GRPC_ERROR_NONE) return err;
    err = prepare_socket(fd, &listener->addr, true, &port);
    if (err != GRPC_ERROR_NONE) return err;
    listener->server->nports++;
    grpc_sockaddr_to_string(&addr_str, &listener->addr, 1);
    gpr_asprintf(&name, "tcp-server-listener:%s/clone-%d", addr_str, i);
    sp = gpr_malloc(sizeof(grpc_tcp_listener));
    sp->next = listener->next;
    listener->next = sp;
    /* sp (the new listener) is a sibling of 'listener' (the original
       listener). */
    sp->is_sibling = 1;
    sp->sibling = listener->sibling;
    listener->sibling = sp;
    sp->server = listener->server;
    sp->fd = fd;
    sp->emfd = grpc_fd_create(fd, name);
    memcpy(&sp->addr, &listener->addr, sizeof(grpc_resolved_address));
    sp->port = port;
    sp->port_index = listener->port_index;
    sp->fd_index = listener->fd_index + count - i;
    GPR_ASSERT(sp->emfd);
    while (listener->server->tail->next != NULL) {
      listener->server->tail = listener->server->tail->next;
    }
    gpr_free(addr_str);
    gpr_free(name);
  }

  return GRPC_ERROR_NONE;
}

grpc_error *grpc_tcp_server_add_port(grpc_tcp_server *s,
                                     const grpc_resolved_address *addr,
                                     int *out_port) {
  grpc_tcp_listener *sp;
  grpc_resolved_address sockname_temp;
  grpc_resolved_address addr6_v4mapped;
  int requested_port = grpc_sockaddr_get_port(addr);
  unsigned port_index = 0;
  grpc_dualstack_mode dsmode;
  grpc_error *err;
  *out_port = -1;
  if (s->tail != NULL) {
    port_index = s->tail->port_index + 1;
  }
  grpc_unlink_if_unix_domain_socket(addr);

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (requested_port == 0) {
    for (sp = s->head; sp; sp = sp->next) {
      sockname_temp.len = sizeof(struct sockaddr_storage);
      if (0 == getsockname(sp->fd, (struct sockaddr *)&sockname_temp.addr,
                           (socklen_t *)&sockname_temp.len)) {
        int used_port = grpc_sockaddr_get_port(&sockname_temp);
        if (used_port > 0) {
          memcpy(&sockname_temp, addr, sizeof(grpc_resolved_address));
          grpc_sockaddr_set_port(&sockname_temp, used_port);
          requested_port = used_port;
          addr = &sockname_temp;
          break;
        }
      }
    }
  }
  if (grpc_sockaddr_is_wildcard(addr, &requested_port)) {
    return add_wildcard_addrs_to_server(s, port_index, requested_port,
                                        out_port);
  }
  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }
  if ((err = add_addr_to_server(s, addr, port_index, 0, &dsmode, &sp)) ==
      GRPC_ERROR_NONE) {
    *out_port = sp->port;
  }
  return err;
}

/* Return listener at port_index or NULL. Should only be called with s->mu
   locked. */
static grpc_tcp_listener *get_port_index(grpc_tcp_server *s,
                                         unsigned port_index) {
  unsigned num_ports = 0;
  grpc_tcp_listener *sp;
  for (sp = s->head; sp; sp = sp->next) {
    if (!sp->is_sibling) {
      if (++num_ports > port_index) {
        return sp;
      }
    }
  }
  return NULL;
}

unsigned grpc_tcp_server_port_fd_count(grpc_tcp_server *s,
                                       unsigned port_index) {
  unsigned num_fds = 0;
  gpr_mu_lock(&s->mu);
  grpc_tcp_listener *sp = get_port_index(s, port_index);
  for (; sp; sp = sp->sibling) {
    ++num_fds;
  }
  gpr_mu_unlock(&s->mu);
  return num_fds;
}

int grpc_tcp_server_port_fd(grpc_tcp_server *s, unsigned port_index,
                            unsigned fd_index) {
  gpr_mu_lock(&s->mu);
  grpc_tcp_listener *sp = get_port_index(s, port_index);
  for (; sp; sp = sp->sibling, --fd_index) {
    if (fd_index == 0) {
      gpr_mu_unlock(&s->mu);
      return sp->fd;
    }
  }
  gpr_mu_unlock(&s->mu);
  return -1;
}

void grpc_tcp_server_start(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s,
                           grpc_pollset **pollsets, size_t pollset_count,
                           grpc_tcp_server_cb on_accept_cb,
                           void *on_accept_cb_arg) {
  size_t i;
  grpc_tcp_listener *sp;
  GPR_ASSERT(on_accept_cb);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->on_accept_cb);
  GPR_ASSERT(s->active_ports == 0);
  s->on_accept_cb = on_accept_cb;
  s->on_accept_cb_arg = on_accept_cb_arg;
  s->pollsets = pollsets;
  s->pollset_count = pollset_count;
  sp = s->head;
  while (sp != NULL) {
    if (s->so_reuseport && !grpc_is_unix_socket(&sp->addr) &&
        pollset_count > 1) {
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "clone_port", clone_port(sp, (unsigned)(pollset_count - 1))));
      for (i = 0; i < pollset_count; i++) {
        grpc_pollset_add_fd(exec_ctx, pollsets[i], sp->emfd);
        grpc_closure_init(&sp->read_closure, on_read, sp,
                          grpc_schedule_on_exec_ctx);
        grpc_fd_notify_on_read(exec_ctx, sp->emfd, &sp->read_closure);
        s->active_ports++;
        sp = sp->next;
      }
    } else {
      for (i = 0; i < pollset_count; i++) {
        grpc_pollset_add_fd(exec_ctx, pollsets[i], sp->emfd);
      }
      grpc_closure_init(&sp->read_closure, on_read, sp,
                        grpc_schedule_on_exec_ctx);
      grpc_fd_notify_on_read(exec_ctx, sp->emfd, &sp->read_closure);
      s->active_ports++;
      sp = sp->next;
    }
  }
  gpr_mu_unlock(&s->mu);
}

grpc_tcp_server *grpc_tcp_server_ref(grpc_tcp_server *s) {
  gpr_ref_non_zero(&s->refs);
  return s;
}

void grpc_tcp_server_shutdown_starting_add(grpc_tcp_server *s,
                                           grpc_closure *shutdown_starting) {
  gpr_mu_lock(&s->mu);
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting,
                           GRPC_ERROR_NONE);
  gpr_mu_unlock(&s->mu);
}

void grpc_tcp_server_unref(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  if (gpr_unref(&s->refs)) {
    grpc_tcp_server_shutdown_listeners(exec_ctx, s);
    gpr_mu_lock(&s->mu);
    grpc_closure_list_sched(exec_ctx, &s->shutdown_starting);
    gpr_mu_unlock(&s->mu);
    tcp_server_destroy(exec_ctx, s);
  }
}

void grpc_tcp_server_shutdown_listeners(grpc_exec_ctx *exec_ctx,
                                        grpc_tcp_server *s) {
  gpr_mu_lock(&s->mu);
  s->shutdown_listeners = true;
  /* shutdown all fd's */
  if (s->active_ports) {
    grpc_tcp_listener *sp;
    for (sp = s->head; sp; sp = sp->next) {
      grpc_fd_shutdown(exec_ctx, sp->emfd,
                       GRPC_ERROR_CREATE("Server shutdown"));
    }
  }
  gpr_mu_unlock(&s->mu);
}

#endif
