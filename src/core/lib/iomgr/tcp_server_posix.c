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

/* FIXME: "posix" files shouldn't be depending on _GNU_SOURCE */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/tcp_server.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/tcp_server_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/string.h"

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
  s->expand_wildcard_addrs = false;
  for (size_t i = 0; i < (args == NULL ? 0 : args->num_args); i++) {
    if (0 == strcmp(GRPC_ARG_ALLOW_REUSEPORT, args->args[i].key)) {
      if (args->args[i].type == GRPC_ARG_INTEGER) {
        s->so_reuseport =
            has_so_reuseport && (args->args[i].value.integer != 0);
      } else {
        gpr_free(s);
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(GRPC_ARG_ALLOW_REUSEPORT
                                                    " must be an integer");
      }
    } else if (0 == strcmp(GRPC_ARG_EXPAND_WILDCARD_ADDRS, args->args[i].key)) {
      if (args->args[i].type == GRPC_ARG_INTEGER) {
        s->expand_wildcard_addrs = (args->args[i].value.integer != 0);
      } else {
        gpr_free(s);
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            GRPC_ARG_EXPAND_WILDCARD_ADDRS " must be an integer");
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
  s->channel_args = grpc_channel_args_copy(args);
  gpr_atm_no_barrier_store(&s->next_pollset_to_assign, 0);
  *server = s;
  return GRPC_ERROR_NONE;
}

static void finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(s->shutdown);
  gpr_mu_unlock(&s->mu);
  if (s->shutdown_complete != NULL) {
    GRPC_CLOSURE_SCHED(exec_ctx, s->shutdown_complete, GRPC_ERROR_NONE);
  }

  gpr_mu_destroy(&s->mu);

  while (s->head) {
    grpc_tcp_listener *sp = s->head;
    s->head = sp->next;
    gpr_free(sp);
  }
  grpc_channel_args_destroy(exec_ctx, s->channel_args);

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
      GRPC_CLOSURE_INIT(&sp->destroyed_closure, destroyed_port, s,
                        grpc_schedule_on_exec_ctx);
      grpc_fd_orphan(exec_ctx, sp->emfd, &sp->destroyed_closure, NULL,
                     false /* already_closed */, "tcp_listener_shutdown");
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
      grpc_fd_shutdown(exec_ctx, sp->emfd, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                               "Server destroyed"));
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    deactivated_all_ports(exec_ctx, s);
  }
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

    if (GRPC_TRACER_ON(grpc_tcp_trace)) {
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
        grpc_tcp_create(exec_ctx, fdobj, sp->server->channel_args, addr_str),
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

  if (grpc_tcp_server_have_ifaddrs() && s->expand_wildcard_addrs) {
    return grpc_tcp_server_add_all_local_addrs(s, port_index, requested_port,
                                               out_port);
  }

  grpc_sockaddr_make_wildcards(requested_port, &wild4, &wild6);
  /* Try listening on IPv6 first. */
  if ((v6_err = grpc_tcp_server_add_addr(s, &wild6, port_index, fd_index,
                                         &dsmode, &sp)) == GRPC_ERROR_NONE) {
    ++fd_index;
    requested_port = *out_port = sp->port;
    if (dsmode == GRPC_DSMODE_DUALSTACK || dsmode == GRPC_DSMODE_IPV4) {
      return GRPC_ERROR_NONE;
    }
  }
  /* If we got a v6-only socket or nothing, try adding 0.0.0.0. */
  grpc_sockaddr_set_port(&wild4, requested_port);
  if ((v4_err = grpc_tcp_server_add_addr(s, &wild4, port_index, fd_index,
                                         &dsmode, &sp2)) == GRPC_ERROR_NONE) {
    *out_port = sp2->port;
    if (sp != NULL) {
      sp2->is_sibling = 1;
      sp->sibling = sp2;
    }
  }
  if (*out_port > 0) {
    if (v6_err != GRPC_ERROR_NONE) {
      gpr_log(GPR_INFO,
              "Failed to add :: listener, "
              "the environment may not support IPv6: %s",
              grpc_error_string(v6_err));
      GRPC_ERROR_UNREF(v6_err);
    }
    if (v4_err != GRPC_ERROR_NONE) {
      gpr_log(GPR_INFO,
              "Failed to add 0.0.0.0 listener, "
              "the environment may not support IPv4: %s",
              grpc_error_string(v4_err));
      GRPC_ERROR_UNREF(v4_err);
    }
    return GRPC_ERROR_NONE;
  } else {
    grpc_error *root_err = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Failed to add any wildcard listeners");
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
    err = grpc_tcp_server_prepare_socket(fd, &listener->addr, true, &port);
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
  if ((err = grpc_tcp_server_add_addr(s, addr, port_index, 0, &dsmode, &sp)) ==
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
        GRPC_CLOSURE_INIT(&sp->read_closure, on_read, sp,
                          grpc_schedule_on_exec_ctx);
        grpc_fd_notify_on_read(exec_ctx, sp->emfd, &sp->read_closure);
        s->active_ports++;
        sp = sp->next;
      }
    } else {
      for (i = 0; i < pollset_count; i++) {
        grpc_pollset_add_fd(exec_ctx, pollsets[i], sp->emfd);
      }
      GRPC_CLOSURE_INIT(&sp->read_closure, on_read, sp,
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
    GRPC_CLOSURE_LIST_SCHED(exec_ctx, &s->shutdown_starting);
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
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Server shutdown"));
    }
  }
  gpr_mu_unlock(&s->mu);
}

#endif
