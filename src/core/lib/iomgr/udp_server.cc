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

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

#include "src/core/lib/iomgr/port.h"

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
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_factory_posix.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"

/* one listening port */
typedef struct grpc_udp_listener grpc_udp_listener;
struct grpc_udp_listener {
  int fd;
  grpc_fd* emfd;
  grpc_udp_server* server;
  grpc_resolved_address addr;
  grpc_closure read_closure;
  grpc_closure write_closure;
  // To be called when corresponding QuicGrpcServer closes all active
  // connections.
  grpc_closure orphan_fd_closure;
  grpc_closure destroyed_closure;
  grpc_udp_server_read_cb read_cb;
  grpc_udp_server_write_cb write_cb;
  grpc_udp_server_orphan_cb orphan_cb;
  grpc_udp_server_start_cb start_cb;
  // To be scheduled on another thread to actually read/write.
  grpc_closure do_read_closure;
  grpc_closure do_write_closure;
  grpc_closure notify_on_write_closure;
  // True if orphan_cb is trigered.
  bool orphan_notified;
  // True if grpc_fd_notify_on_write() is called after on_write() call.
  bool notify_on_write_armed;
  // True if fd has been shutdown.
  bool already_shutdown;

  struct grpc_udp_listener* next;
};

struct shutdown_fd_args {
  grpc_udp_listener* sp;
  gpr_mu* server_mu;
};

/* the overall server */
struct grpc_udp_server {
  gpr_mu mu;

  /* factory to use for creating and binding sockets, or NULL */
  grpc_socket_factory* socket_factory;

  /* active port count: how many ports are actually still listening */
  size_t active_ports;
  /* destroyed port count: how many ports are completely destroyed */
  size_t destroyed_ports;

  /* is this server shutting down? (boolean) */
  int shutdown;

  /* linked list of server ports */
  grpc_udp_listener* head;
  grpc_udp_listener* tail;
  unsigned nports;

  /* shutdown callback */
  grpc_closure* shutdown_complete;

  /* all pollsets interested in new connections */
  grpc_pollset** pollsets;
  /* number of pollsets in the pollsets array */
  size_t pollset_count;
  /* opaque object to pass to callbacks */
  void* user_data;
};

static grpc_socket_factory* get_socket_factory(const grpc_channel_args* args) {
  if (args) {
    const grpc_arg* arg = grpc_channel_args_find(args, GRPC_ARG_SOCKET_FACTORY);
    if (arg) {
      GPR_ASSERT(arg->type == GRPC_ARG_POINTER);
      return (grpc_socket_factory*)arg->value.pointer.p;
    }
  }
  return nullptr;
}

grpc_udp_server* grpc_udp_server_create(const grpc_channel_args* args) {
  grpc_udp_server* s = (grpc_udp_server*)gpr_malloc(sizeof(grpc_udp_server));
  gpr_mu_init(&s->mu);
  s->socket_factory = get_socket_factory(args);
  if (s->socket_factory) {
    grpc_socket_factory_ref(s->socket_factory);
  }
  s->active_ports = 0;
  s->destroyed_ports = 0;
  s->shutdown = 0;
  s->head = nullptr;
  s->tail = nullptr;
  s->nports = 0;

  return s;
}

static void shutdown_fd(void* args, grpc_error* error) {
  struct shutdown_fd_args* shutdown_args = (struct shutdown_fd_args*)args;
  grpc_udp_listener* sp = shutdown_args->sp;
  gpr_log(GPR_DEBUG, "shutdown fd %d", sp->fd);
  gpr_mu_lock(shutdown_args->server_mu);
  grpc_fd_shutdown(sp->emfd, GRPC_ERROR_REF(error));
  sp->already_shutdown = true;
  if (!sp->notify_on_write_armed) {
    // Re-arm write notification to notify listener with error. This is
    // necessary to decrement active_ports.
    sp->notify_on_write_armed = true;
    grpc_fd_notify_on_write(sp->emfd, &sp->write_closure);
  }
  gpr_mu_unlock(shutdown_args->server_mu);
  gpr_free(shutdown_args);
}

static void dummy_cb(void* arg, grpc_error* error) {
  // No-op.
}

static void finish_shutdown(grpc_udp_server* s) {
  if (s->shutdown_complete != nullptr) {
    GRPC_CLOSURE_SCHED(s->shutdown_complete, GRPC_ERROR_NONE);
  }

  gpr_mu_destroy(&s->mu);

  gpr_log(GPR_DEBUG, "Destroy all listeners.");
  while (s->head) {
    grpc_udp_listener* sp = s->head;
    s->head = sp->next;
    gpr_free(sp);
  }

  if (s->socket_factory) {
    grpc_socket_factory_unref(s->socket_factory);
  }

  gpr_free(s);
}

static void destroyed_port(void* server, grpc_error* error) {
  grpc_udp_server* s = (grpc_udp_server*)server;
  gpr_mu_lock(&s->mu);
  s->destroyed_ports++;
  if (s->destroyed_ports == s->nports) {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(s);
  } else {
    gpr_mu_unlock(&s->mu);
  }
}

/* called when all listening endpoints have been shutdown, so no further
   events will be received on them - at this point it's safe to destroy
   things */
static void deactivated_all_ports(grpc_udp_server* s) {
  /* delete ALL the things */
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(s->shutdown);

  if (s->head) {
    grpc_udp_listener* sp;
    for (sp = s->head; sp; sp = sp->next) {
      grpc_unlink_if_unix_domain_socket(&sp->addr);

      GRPC_CLOSURE_INIT(&sp->destroyed_closure, destroyed_port, s,
                        grpc_schedule_on_exec_ctx);
      if (!sp->orphan_notified) {
        /* Call the orphan_cb to signal that the FD is about to be closed and
         * should no longer be used. Because at this point, all listening ports
         * have been shutdown already, no need to shutdown again.*/
        GRPC_CLOSURE_INIT(&sp->orphan_fd_closure, dummy_cb, sp,
                          grpc_schedule_on_exec_ctx);
        GPR_ASSERT(sp->orphan_cb);
        gpr_log(GPR_DEBUG, "Orphan fd %d", sp->fd);
        sp->orphan_cb(sp->emfd, &sp->orphan_fd_closure, sp->server->user_data);
      }
      grpc_fd_orphan(sp->emfd, &sp->destroyed_closure, nullptr,
                     false /* already_closed */, "udp_listener_shutdown");
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(s);
  }
}

void grpc_udp_server_destroy(grpc_udp_server* s, grpc_closure* on_done) {
  grpc_udp_listener* sp;
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(!s->shutdown);
  s->shutdown = 1;

  s->shutdown_complete = on_done;

  gpr_log(GPR_DEBUG, "start to destroy udp_server");
  /* shutdown all fd's */
  if (s->active_ports) {
    for (sp = s->head; sp; sp = sp->next) {
      GPR_ASSERT(sp->orphan_cb);
      struct shutdown_fd_args* args =
          (struct shutdown_fd_args*)gpr_malloc(sizeof(*args));
      args->sp = sp;
      args->server_mu = &s->mu;
      GRPC_CLOSURE_INIT(&sp->orphan_fd_closure, shutdown_fd, args,
                        grpc_schedule_on_exec_ctx);
      sp->orphan_cb(sp->emfd, &sp->orphan_fd_closure, sp->server->user_data);
      sp->orphan_notified = true;
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    deactivated_all_ports(s);
  }
}

static int bind_socket(grpc_socket_factory* socket_factory, int sockfd,
                       const grpc_resolved_address* addr) {
  return (socket_factory != nullptr)
             ? grpc_socket_factory_bind(socket_factory, sockfd, addr)
             : bind(sockfd, (struct sockaddr*)addr->addr, (socklen_t)addr->len);
}

/* Prepare a recently-created socket for listening. */
static int prepare_socket(grpc_socket_factory* socket_factory, int fd,
                          const grpc_resolved_address* addr, int rcv_buf_size,
                          int snd_buf_size) {
  grpc_resolved_address sockname_temp;
  struct sockaddr* addr_ptr = (struct sockaddr*)addr->addr;

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
  } else if (addr_ptr->sa_family == AF_INET6) {
    if (grpc_set_socket_ipv6_recvpktinfo_if_possible(fd) != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Unable to set ipv6_recvpktinfo.");
      goto error;
    }
  }

  GPR_ASSERT(addr->len < ~(socklen_t)0);
  if (bind_socket(socket_factory, fd, addr) < 0) {
    char* addr_str;
    grpc_sockaddr_to_string(&addr_str, addr, 0);
    gpr_log(GPR_ERROR, "bind addr=%s: %s", addr_str, strerror(errno));
    gpr_free(addr_str);
    goto error;
  }

  sockname_temp.len = sizeof(struct sockaddr_storage);

  if (getsockname(fd, (struct sockaddr*)sockname_temp.addr,
                  (socklen_t*)&sockname_temp.len) < 0) {
    goto error;
  }

  if (grpc_set_socket_sndbuf(fd, snd_buf_size) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Failed to set send buffer size to %d bytes",
            snd_buf_size);
    goto error;
  }

  if (grpc_set_socket_rcvbuf(fd, rcv_buf_size) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Failed to set receive buffer size to %d bytes",
            rcv_buf_size);
    goto error;
  }

  {
    int get_overflow = 1;
    if (0 != setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &get_overflow,
                        sizeof(get_overflow))) {
      gpr_log(GPR_INFO, "Failed to set socket overflow support");
    }
  }
  return grpc_sockaddr_get_port(&sockname_temp);

error:
  if (fd >= 0) {
    close(fd);
  }
  return -1;
}

static void do_read(void* arg, grpc_error* error) {
  grpc_udp_listener* sp = reinterpret_cast<grpc_udp_listener*>(arg);
  GPR_ASSERT(sp->read_cb && error == GRPC_ERROR_NONE);
  /* TODO: the reason we hold server->mu here is merely to prevent fd
   * shutdown while we are reading. However, it blocks do_write(). Switch to
   * read lock if available. */
  gpr_mu_lock(&sp->server->mu);
  /* Tell the registered callback that data is available to read. */
  if (!sp->already_shutdown && sp->read_cb(sp->emfd)) {
    /* There maybe more packets to read. Schedule read_more_cb_ closure to run
     * after finishing this event loop. */
    GRPC_CLOSURE_SCHED(&sp->do_read_closure, GRPC_ERROR_NONE);
  } else {
    /* Finish reading all the packets, re-arm the notification event so we can
     * get another chance to read. Or fd already shutdown, re-arm to get a
     * notification with shutdown error. */
    grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
  }
  gpr_mu_unlock(&sp->server->mu);
}

/* event manager callback when reads are ready */
static void on_read(void* arg, grpc_error* error) {
  grpc_udp_listener* sp = (grpc_udp_listener*)arg;

  gpr_mu_lock(&sp->server->mu);
  if (error != GRPC_ERROR_NONE) {
    if (0 == --sp->server->active_ports && sp->server->shutdown) {
      gpr_mu_unlock(&sp->server->mu);
      deactivated_all_ports(sp->server);
    } else {
      gpr_mu_unlock(&sp->server->mu);
    }
    return;
  }
  /* Read once. If there is more data to read, off load the work to another
   * thread to finish. */
  GPR_ASSERT(sp->read_cb);
  if (sp->read_cb(sp->emfd)) {
    /* There maybe more packets to read. Schedule read_more_cb_ closure to run
     * after finishing this event loop. */
    GRPC_CLOSURE_INIT(&sp->do_read_closure, do_read, arg,
                      grpc_executor_scheduler(GRPC_EXECUTOR_LONG));
    GRPC_CLOSURE_SCHED(&sp->do_read_closure, GRPC_ERROR_NONE);
  } else {
    /* Finish reading all the packets, re-arm the notification event so we can
     * get another chance to read. Or fd already shutdown, re-arm to get a
     * notification with shutdown error. */
    grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
  }
  gpr_mu_unlock(&sp->server->mu);
}

// Wrapper of grpc_fd_notify_on_write() with a grpc_closure callback interface.
void fd_notify_on_write_wrapper(void* arg, grpc_error* error) {
  grpc_udp_listener* sp = reinterpret_cast<grpc_udp_listener*>(arg);
  gpr_mu_lock(&sp->server->mu);
  if (!sp->notify_on_write_armed) {
    grpc_fd_notify_on_write(sp->emfd, &sp->write_closure);
    sp->notify_on_write_armed = true;
  }
  gpr_mu_unlock(&sp->server->mu);
}

static void do_write(void* arg, grpc_error* error) {
  grpc_udp_listener* sp = reinterpret_cast<grpc_udp_listener*>(arg);
  gpr_mu_lock(&sp->server->mu);
  if (sp->already_shutdown) {
    // If fd has been shutdown, don't write any more and re-arm notification.
    grpc_fd_notify_on_write(sp->emfd, &sp->write_closure);
  } else {
    sp->notify_on_write_armed = false;
    /* Tell the registered callback that the socket is writeable. */
    GPR_ASSERT(sp->write_cb && error == GRPC_ERROR_NONE);
    GRPC_CLOSURE_INIT(&sp->notify_on_write_closure, fd_notify_on_write_wrapper,
                      arg, grpc_schedule_on_exec_ctx);
    sp->write_cb(sp->emfd, sp->server->user_data, &sp->notify_on_write_closure);
  }
  gpr_mu_unlock(&sp->server->mu);
}

static void on_write(void* arg, grpc_error* error) {
  grpc_udp_listener* sp = (grpc_udp_listener*)arg;

  gpr_mu_lock(&sp->server->mu);
  if (error != GRPC_ERROR_NONE) {
    if (0 == --sp->server->active_ports && sp->server->shutdown) {
      gpr_mu_unlock(&sp->server->mu);
      deactivated_all_ports(sp->server);
    } else {
      gpr_mu_unlock(&sp->server->mu);
    }
    return;
  }

  /* Schedule actual write in another thread. */
  GRPC_CLOSURE_INIT(&sp->do_write_closure, do_write, arg,
                    grpc_executor_scheduler(GRPC_EXECUTOR_LONG));

  GRPC_CLOSURE_SCHED(&sp->do_write_closure, GRPC_ERROR_NONE);
  gpr_mu_unlock(&sp->server->mu);
}

static int add_socket_to_server(grpc_udp_server* s, int fd,
                                const grpc_resolved_address* addr,
                                int rcv_buf_size, int snd_buf_size,
                                grpc_udp_server_start_cb start_cb,
                                grpc_udp_server_read_cb read_cb,
                                grpc_udp_server_write_cb write_cb,
                                grpc_udp_server_orphan_cb orphan_cb) {
  grpc_udp_listener* sp;
  int port;
  char* addr_str;
  char* name;

  port =
      prepare_socket(s->socket_factory, fd, addr, rcv_buf_size, snd_buf_size);
  if (port >= 0) {
    grpc_sockaddr_to_string(&addr_str, addr, 1);
    gpr_asprintf(&name, "udp-server-listener:%s", addr_str);
    gpr_free(addr_str);
    gpr_mu_lock(&s->mu);
    s->nports++;
    sp = (grpc_udp_listener*)gpr_malloc(sizeof(grpc_udp_listener));
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
    sp->read_cb = read_cb;
    sp->write_cb = write_cb;
    sp->orphan_cb = orphan_cb;
    sp->start_cb = start_cb;
    sp->orphan_notified = false;
    sp->already_shutdown = false;
    GPR_ASSERT(sp->emfd);
    gpr_mu_unlock(&s->mu);
    gpr_free(name);
  }

  return port;
}

int grpc_udp_server_add_port(grpc_udp_server* s,
                             const grpc_resolved_address* addr,
                             int rcv_buf_size, int snd_buf_size,
                             grpc_udp_server_start_cb start_cb,
                             grpc_udp_server_read_cb read_cb,
                             grpc_udp_server_write_cb write_cb,
                             grpc_udp_server_orphan_cb orphan_cb) {
  grpc_udp_listener* sp;
  int allocated_port1 = -1;
  int allocated_port2 = -1;
  int fd;
  grpc_dualstack_mode dsmode;
  grpc_resolved_address addr6_v4mapped;
  grpc_resolved_address wild4;
  grpc_resolved_address wild6;
  grpc_resolved_address addr4_copy;
  grpc_resolved_address* allocated_addr = nullptr;
  grpc_resolved_address sockname_temp;
  int port;

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (sp = s->head; sp; sp = sp->next) {
      sockname_temp.len = sizeof(struct sockaddr_storage);
      if (0 == getsockname(sp->fd, (struct sockaddr*)sockname_temp.addr,
                           (socklen_t*)&sockname_temp.len)) {
        port = grpc_sockaddr_get_port(&sockname_temp);
        if (port > 0) {
          allocated_addr =
              (grpc_resolved_address*)gpr_malloc(sizeof(grpc_resolved_address));
          memcpy(allocated_addr, addr, sizeof(grpc_resolved_address));
          grpc_sockaddr_set_port(allocated_addr, port);
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
  if (grpc_sockaddr_is_wildcard(addr, &port)) {
    grpc_sockaddr_make_wildcards(port, &wild4, &wild6);

    /* Try listening on IPv6 first. */
    addr = &wild6;
    // TODO(rjshade): Test and propagate the returned grpc_error*:
    GRPC_ERROR_UNREF(grpc_create_dualstack_socket_using_factory(
        s->socket_factory, addr, SOCK_DGRAM, IPPROTO_UDP, &dsmode, &fd));
    allocated_port1 =
        add_socket_to_server(s, fd, addr, rcv_buf_size, snd_buf_size, start_cb,
                             read_cb, write_cb, orphan_cb);
    if (fd >= 0 && dsmode == GRPC_DSMODE_DUALSTACK) {
      goto done;
    }

    /* If we didn't get a dualstack socket, also listen on 0.0.0.0. */
    if (port == 0 && allocated_port1 > 0) {
      grpc_sockaddr_set_port(&wild4, allocated_port1);
    }
    addr = &wild4;
  }

  // TODO(rjshade): Test and propagate the returned grpc_error*:
  GRPC_ERROR_UNREF(grpc_create_dualstack_socket_using_factory(
      s->socket_factory, addr, SOCK_DGRAM, IPPROTO_UDP, &dsmode, &fd));
  if (fd < 0) {
    gpr_log(GPR_ERROR, "Unable to create socket: %s", strerror(errno));
  }
  if (dsmode == GRPC_DSMODE_IPV4 &&
      grpc_sockaddr_is_v4mapped(addr, &addr4_copy)) {
    addr = &addr4_copy;
  }
  allocated_port2 =
      add_socket_to_server(s, fd, addr, rcv_buf_size, snd_buf_size, start_cb,
                           read_cb, write_cb, orphan_cb);

done:
  gpr_free(allocated_addr);
  return allocated_port1 >= 0 ? allocated_port1 : allocated_port2;
}

int grpc_udp_server_get_fd(grpc_udp_server* s, unsigned port_index) {
  grpc_udp_listener* sp;
  if (port_index >= s->nports) {
    return -1;
  }

  for (sp = s->head; sp && port_index != 0; sp = sp->next) {
    --port_index;
  }
  GPR_ASSERT(sp);  // if this fails, our check earlier was bogus
  return sp->fd;
}

void grpc_udp_server_start(grpc_udp_server* s, grpc_pollset** pollsets,
                           size_t pollset_count, void* user_data) {
  gpr_log(GPR_DEBUG, "grpc_udp_server_start");
  size_t i;
  gpr_mu_lock(&s->mu);
  grpc_udp_listener* sp;
  GPR_ASSERT(s->active_ports == 0);
  s->pollsets = pollsets;
  s->user_data = user_data;

  sp = s->head;
  while (sp != nullptr) {
    sp->start_cb(sp->emfd, sp->server->user_data);
    for (i = 0; i < pollset_count; i++) {
      grpc_pollset_add_fd(pollsets[i], sp->emfd);
    }
    GRPC_CLOSURE_INIT(&sp->read_closure, on_read, sp,
                      grpc_schedule_on_exec_ctx);
    grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);

    GRPC_CLOSURE_INIT(&sp->write_closure, on_write, sp,
                      grpc_schedule_on_exec_ctx);
    sp->notify_on_write_armed = true;
    grpc_fd_notify_on_write(sp->emfd, &sp->write_closure);

    /* Registered for both read and write callbacks: increment active_ports
     * twice to account for this, and delay free-ing of memory until both
     * on_read and on_write have fired. */
    s->active_ports += 2;

    sp = sp->next;
  }

  gpr_mu_unlock(&s->mu);
}

#endif
