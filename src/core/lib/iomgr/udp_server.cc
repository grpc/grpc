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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_UDP_SERVER

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

#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_factory_posix.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"

/* A listener which implements basic features of Listening on a port for
 * I/O events*/
class GrpcUdpListener {
 public:
  GrpcUdpListener(grpc_udp_server* server, int fd,
                  const grpc_resolved_address* addr);
  ~GrpcUdpListener();

  /* Called when grpc server starts to listening on the grpc_fd. */
  void StartListening(const std::vector<grpc_pollset*>* pollsets,
                      GrpcUdpHandlerFactory* handler_factory);

  /* Called when data is available to read from the socket.
   * Return true if there is more data to read from fd. */
  void OnRead(grpc_error* error, void* do_read_arg);

  /* Called when the socket is writeable. The given closure should be scheduled
   * when the socket becomes blocked next time. */
  void OnCanWrite(grpc_error* error, void* do_write_arg);

  /* Called when the grpc_fd is about to be orphaned (and the FD closed). */
  void OnFdAboutToOrphan();

  /* Called to orphan fd of this listener.*/
  void OrphanFd();

  /* Called when this listener is going to be destroyed. */
  void OnDestroy();

  int fd() const { return fd_; }

 protected:
  grpc_fd* emfd() const { return emfd_; }

  gpr_mu* mutex() { return &mutex_; }

 private:
  /* event manager callback when reads are ready */
  static void on_read(void* arg, grpc_error* error);
  static void on_write(void* arg, grpc_error* error);

  static void do_read(void* arg, grpc_error* error);
  static void do_write(void* arg, grpc_error* error);
  // Wrapper of grpc_fd_notify_on_write() with a grpc_closure callback
  // interface.
  static void fd_notify_on_write_wrapper(void* arg, grpc_error* error);

  static void shutdown_fd(void* args, grpc_error* error);

  int fd_;
  grpc_fd* emfd_;
  grpc_udp_server* server_;
  grpc_resolved_address addr_;
  grpc_closure read_closure_;
  grpc_closure write_closure_;
  // To be called when corresponding QuicGrpcServer closes all active
  // connections.
  grpc_closure orphan_fd_closure_;
  grpc_closure destroyed_closure_;
  // To be scheduled on another thread to actually read/write.
  grpc_closure do_read_closure_;
  grpc_closure do_write_closure_;
  grpc_closure notify_on_write_closure_;
  // True if orphan_cb is trigered.
  bool orphan_notified_;
  // True if grpc_fd_notify_on_write() is called after on_write() call.
  bool notify_on_write_armed_;
  // True if fd has been shutdown.
  bool already_shutdown_;
  // Object actually handles I/O events. Assigned in StartListening().
  GrpcUdpHandler* udp_handler_ = nullptr;
  // To be notified on destruction.
  GrpcUdpHandlerFactory* handler_factory_ = nullptr;
  // Required to access above fields.
  gpr_mu mutex_;
};

GrpcUdpListener::GrpcUdpListener(grpc_udp_server* server, int fd,
                                 const grpc_resolved_address* addr)
    : fd_(fd),
      server_(server),
      orphan_notified_(false),
      already_shutdown_(false) {
  std::string addr_str = grpc_sockaddr_to_string(addr, true);
  std::string name = absl::StrCat("udp-server-listener:", addr_str);
  emfd_ = grpc_fd_create(fd, name.c_str(), true);
  memcpy(&addr_, addr, sizeof(grpc_resolved_address));
  GPR_ASSERT(emfd_);
  gpr_mu_init(&mutex_);
}

GrpcUdpListener::~GrpcUdpListener() { gpr_mu_destroy(&mutex_); }

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

  /* An array of listeners */
  absl::InlinedVector<GrpcUdpListener, 16> listeners;

  /* factory for use to create udp listeners */
  GrpcUdpHandlerFactory* handler_factory;

  /* shutdown callback */
  grpc_closure* shutdown_complete;

  /* all pollsets interested in new connections. The object pointed at is not
   * owned by this struct. */
  const std::vector<grpc_pollset*>* pollsets;
  /* opaque object to pass to callbacks */
  void* user_data;

  /* latch has_so_reuseport during server creation */
  bool so_reuseport;
};

static grpc_socket_factory* get_socket_factory(const grpc_channel_args* args) {
  if (args) {
    const grpc_arg* arg = grpc_channel_args_find(args, GRPC_ARG_SOCKET_FACTORY);
    if (arg) {
      GPR_ASSERT(arg->type == GRPC_ARG_POINTER);
      return static_cast<grpc_socket_factory*>(arg->value.pointer.p);
    }
  }
  return nullptr;
}

grpc_udp_server* grpc_udp_server_create(const grpc_channel_args* args) {
  grpc_udp_server* s = new grpc_udp_server();
  gpr_mu_init(&s->mu);
  s->socket_factory = get_socket_factory(args);
  if (s->socket_factory) {
    grpc_socket_factory_ref(s->socket_factory);
  }
  s->active_ports = 0;
  s->destroyed_ports = 0;
  s->shutdown = 0;
  s->so_reuseport = grpc_is_socket_reuse_port_supported();
  return s;
}

// static
void GrpcUdpListener::shutdown_fd(void* args, grpc_error* error) {
  if (args == nullptr) {
    // No-op if shutdown args are null.
    return;
  }
  auto sp = static_cast<GrpcUdpListener*>(args);
  gpr_mu_lock(sp->mutex());
  gpr_log(GPR_DEBUG, "shutdown fd %d", sp->fd_);
  grpc_fd_shutdown(sp->emfd_, GRPC_ERROR_REF(error));
  sp->already_shutdown_ = true;
  if (!sp->notify_on_write_armed_) {
    // Re-arm write notification to notify listener with error. This is
    // necessary to decrement active_ports.
    sp->notify_on_write_armed_ = true;
    grpc_fd_notify_on_write(sp->emfd_, &sp->write_closure_);
  }
  gpr_mu_unlock(sp->mutex());
}

static void finish_shutdown(grpc_udp_server* s) {
  if (s->shutdown_complete != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, s->shutdown_complete,
                            GRPC_ERROR_NONE);
  }

  gpr_mu_destroy(&s->mu);

  gpr_log(GPR_DEBUG, "Destroy all listeners.");
  for (size_t i = 0; i < s->listeners.size(); ++i) {
    s->listeners[i].OnDestroy();
  }

  if (s->socket_factory) {
    grpc_socket_factory_unref(s->socket_factory);
  }

  delete s;
}

static void destroyed_port(void* server, grpc_error* /*error*/) {
  grpc_udp_server* s = static_cast<grpc_udp_server*>(server);
  gpr_mu_lock(&s->mu);
  s->destroyed_ports++;
  if (s->destroyed_ports == s->listeners.size()) {
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

  if (s->listeners.empty()) {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(s);
    return;
  }
  for (size_t i = 0; i < s->listeners.size(); ++i) {
    s->listeners[i].OrphanFd();
  }
  gpr_mu_unlock(&s->mu);
}

void GrpcUdpListener::OrphanFd() {
  gpr_log(GPR_DEBUG, "Orphan fd %d, emfd %p", fd_, emfd_);
  grpc_unlink_if_unix_domain_socket(&addr_);

  GRPC_CLOSURE_INIT(&destroyed_closure_, destroyed_port, server_,
                    grpc_schedule_on_exec_ctx);
  /* Because at this point, all listening sockets have been shutdown already, no
   * need to call OnFdAboutToOrphan() to notify the handler again. */
  grpc_fd_orphan(emfd_, &destroyed_closure_, nullptr, "udp_listener_shutdown");
}

void grpc_udp_server_destroy(grpc_udp_server* s, grpc_closure* on_done) {
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(!s->shutdown);
  s->shutdown = 1;

  s->shutdown_complete = on_done;

  gpr_log(GPR_DEBUG, "start to destroy udp_server");
  /* shutdown all fd's */
  if (s->active_ports) {
    for (size_t i = 0; i < s->listeners.size(); ++i) {
      GrpcUdpListener* sp = &s->listeners[i];
      sp->OnFdAboutToOrphan();
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    deactivated_all_ports(s);
  }
}

void GrpcUdpListener::OnFdAboutToOrphan() {
  gpr_mu_lock(&mutex_);
  grpc_unlink_if_unix_domain_socket(&addr_);

  GRPC_CLOSURE_INIT(&destroyed_closure_, destroyed_port, server_,
                    grpc_schedule_on_exec_ctx);
  if (!orphan_notified_ && udp_handler_ != nullptr) {
    /* Signals udp_handler that the FD is about to be closed and
     * should no longer be used. */
    GRPC_CLOSURE_INIT(&orphan_fd_closure_, shutdown_fd, this,
                      grpc_schedule_on_exec_ctx);
    gpr_log(GPR_DEBUG, "fd %d about to be orphaned", fd_);
    udp_handler_->OnFdAboutToOrphan(&orphan_fd_closure_, server_->user_data);
    orphan_notified_ = true;
  }
  gpr_mu_unlock(&mutex_);
}

static int bind_socket(grpc_socket_factory* socket_factory, int sockfd,
                       const grpc_resolved_address* addr) {
  return (socket_factory != nullptr)
             ? grpc_socket_factory_bind(socket_factory, sockfd, addr)
             : bind(sockfd,
                    reinterpret_cast<grpc_sockaddr*>(
                        const_cast<char*>(addr->addr)),
                    addr->len);
}

/* Prepare a recently-created socket for listening. */
static int prepare_socket(grpc_socket_factory* socket_factory, int fd,
                          const grpc_resolved_address* addr, int rcv_buf_size,
                          int snd_buf_size, bool so_reuseport) {
  grpc_resolved_address sockname_temp;
  grpc_sockaddr* addr_ptr =
      reinterpret_cast<grpc_sockaddr*>(const_cast<char*>(addr->addr));

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

  if (so_reuseport && !grpc_is_unix_socket(addr) &&
      grpc_set_socket_reuse_port(fd, 1) != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Failed to set SO_REUSEPORT for fd %d", fd);
    goto error;
  }

  if (bind_socket(socket_factory, fd, addr) < 0) {
    std::string addr_str = grpc_sockaddr_to_string(addr, false);
    gpr_log(GPR_ERROR, "bind addr=%s: %s", addr_str.c_str(), strerror(errno));
    goto error;
  }

  sockname_temp.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));

  if (getsockname(fd, reinterpret_cast<grpc_sockaddr*>(sockname_temp.addr),
                  &sockname_temp.len) < 0) {
    gpr_log(GPR_ERROR, "Unable to get the address socket %d is bound to: %s",
            fd, strerror(errno));
    goto error;
  }

  return grpc_sockaddr_get_port(&sockname_temp);

error:
  if (fd >= 0) {
    close(fd);
  }
  return -1;
}

// static
void GrpcUdpListener::do_read(void* arg, grpc_error* error) {
  GrpcUdpListener* sp = static_cast<GrpcUdpListener*>(arg);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  /* TODO: the reason we hold server->mu here is merely to prevent fd
   * shutdown while we are reading. However, it blocks do_write(). Switch to
   * read lock if available. */
  gpr_mu_lock(sp->mutex());
  /* Tell the registered callback that data is available to read. */
  if (!sp->already_shutdown_ && sp->udp_handler_->Read()) {
    /* There maybe more packets to read. Schedule read_more_cb_ closure to run
     * after finishing this event loop. */
    grpc_core::Executor::Run(&sp->do_read_closure_, GRPC_ERROR_NONE,
                             grpc_core::ExecutorType::DEFAULT,
                             grpc_core::ExecutorJobType::LONG);
  } else {
    /* Finish reading all the packets, re-arm the notification event so we can
     * get another chance to read. Or fd already shutdown, re-arm to get a
     * notification with shutdown error. */
    grpc_fd_notify_on_read(sp->emfd_, &sp->read_closure_);
  }
  gpr_mu_unlock(sp->mutex());
}

// static
void GrpcUdpListener::on_read(void* arg, grpc_error* error) {
  GrpcUdpListener* sp = static_cast<GrpcUdpListener*>(arg);
  sp->OnRead(error, arg);
}

void GrpcUdpListener::OnRead(grpc_error* error, void* do_read_arg) {
  if (error != GRPC_ERROR_NONE) {
    gpr_mu_lock(&server_->mu);
    if (0 == --server_->active_ports && server_->shutdown) {
      gpr_mu_unlock(&server_->mu);
      deactivated_all_ports(server_);
    } else {
      gpr_mu_unlock(&server_->mu);
    }
    return;
  }

  /* Read once. If there is more data to read, off load the work to another
   * thread to finish. */
  if (udp_handler_->Read()) {
    /* There maybe more packets to read. Schedule read_more_cb_ closure to run
     * after finishing this event loop. */
    GRPC_CLOSURE_INIT(&do_read_closure_, do_read, do_read_arg, nullptr);
    grpc_core::Executor::Run(&do_read_closure_, GRPC_ERROR_NONE,
                             grpc_core::ExecutorType::DEFAULT,
                             grpc_core::ExecutorJobType::LONG);
  } else {
    /* Finish reading all the packets, re-arm the notification event so we can
     * get another chance to read. Or fd already shutdown, re-arm to get a
     * notification with shutdown error. */
    grpc_fd_notify_on_read(emfd_, &read_closure_);
  }
}

// static
// Wrapper of grpc_fd_notify_on_write() with a grpc_closure callback interface.
void GrpcUdpListener::fd_notify_on_write_wrapper(void* arg,
                                                 grpc_error* /*error*/) {
  GrpcUdpListener* sp = static_cast<GrpcUdpListener*>(arg);
  gpr_mu_lock(sp->mutex());
  if (!sp->notify_on_write_armed_) {
    grpc_fd_notify_on_write(sp->emfd_, &sp->write_closure_);
    sp->notify_on_write_armed_ = true;
  }
  gpr_mu_unlock(sp->mutex());
}

// static
void GrpcUdpListener::do_write(void* arg, grpc_error* error) {
  GrpcUdpListener* sp = static_cast<GrpcUdpListener*>(arg);
  gpr_mu_lock(sp->mutex());
  if (sp->already_shutdown_) {
    // If fd has been shutdown, don't write any more and re-arm notification.
    grpc_fd_notify_on_write(sp->emfd_, &sp->write_closure_);
  } else {
    sp->notify_on_write_armed_ = false;
    /* Tell the registered callback that the socket is writeable. */
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    GRPC_CLOSURE_INIT(&sp->notify_on_write_closure_, fd_notify_on_write_wrapper,
                      arg, grpc_schedule_on_exec_ctx);
    sp->udp_handler_->OnCanWrite(sp->server_->user_data,
                                 &sp->notify_on_write_closure_);
  }
  gpr_mu_unlock(sp->mutex());
}

// static
void GrpcUdpListener::on_write(void* arg, grpc_error* error) {
  GrpcUdpListener* sp = static_cast<GrpcUdpListener*>(arg);
  sp->OnCanWrite(error, arg);
}

void GrpcUdpListener::OnCanWrite(grpc_error* error, void* do_write_arg) {
  if (error != GRPC_ERROR_NONE) {
    gpr_mu_lock(&server_->mu);
    if (0 == --server_->active_ports && server_->shutdown) {
      gpr_mu_unlock(&server_->mu);
      deactivated_all_ports(server_);
    } else {
      gpr_mu_unlock(&server_->mu);
    }
    return;
  }

  /* Schedule actual write in another thread. */
  GRPC_CLOSURE_INIT(&do_write_closure_, do_write, do_write_arg, nullptr);

  grpc_core::Executor::Run(&do_write_closure_, GRPC_ERROR_NONE,
                           grpc_core::ExecutorType::DEFAULT,
                           grpc_core::ExecutorJobType::LONG);
}

static int add_socket_to_server(grpc_udp_server* s, int fd,
                                const grpc_resolved_address* addr,
                                int rcv_buf_size, int snd_buf_size) {
  gpr_log(GPR_DEBUG, "add socket %d to server", fd);

  int port = prepare_socket(s->socket_factory, fd, addr, rcv_buf_size,
                            snd_buf_size, s->so_reuseport);
  if (port >= 0) {
    gpr_mu_lock(&s->mu);
    s->listeners.emplace_back(s, fd, addr);
    gpr_log(GPR_DEBUG,
            "add socket %d to server for port %d, %zu listener(s) in total", fd,
            port, s->listeners.size());
    gpr_mu_unlock(&s->mu);
  }
  return port;
}

int grpc_udp_server_add_port(grpc_udp_server* s,
                             const grpc_resolved_address* addr,
                             int rcv_buf_size, int snd_buf_size,
                             GrpcUdpHandlerFactory* handler_factory,
                             size_t num_listeners) {
  if (num_listeners > 1 && !s->so_reuseport) {
    gpr_log(GPR_ERROR,
            "Try to have multiple listeners on same port, but SO_REUSEPORT is "
            "not supported. Only create 1 listener.");
  }
  std::string addr_str = grpc_sockaddr_to_string(addr, true);
  gpr_log(GPR_DEBUG, "add address: %s to server", addr_str.c_str());

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
  int port = 0;

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    /* Loop through existing listeners to find the port in use. */
    for (size_t i = 0; i < s->listeners.size(); ++i) {
      sockname_temp.len =
          static_cast<socklen_t>(sizeof(struct sockaddr_storage));
      if (0 == getsockname(s->listeners[i].fd(),
                           reinterpret_cast<grpc_sockaddr*>(sockname_temp.addr),
                           &sockname_temp.len)) {
        port = grpc_sockaddr_get_port(&sockname_temp);
        if (port > 0) {
          /* Found such a port, update |addr| to reflects this port. */
          allocated_addr = static_cast<grpc_resolved_address*>(
              gpr_malloc(sizeof(grpc_resolved_address)));
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

  s->handler_factory = handler_factory;
  for (size_t i = 0; i < num_listeners; ++i) {
    /* Treat :: or 0.0.0.0 as a family-agnostic wildcard. */
    if (grpc_sockaddr_is_wildcard(addr, &port)) {
      grpc_sockaddr_make_wildcards(port, &wild4, &wild6);

      /* Try listening on IPv6 first. */
      addr = &wild6;
      // TODO(rjshade): Test and propagate the returned grpc_error*:
      GRPC_ERROR_UNREF(grpc_create_dualstack_socket_using_factory(
          s->socket_factory, addr, SOCK_DGRAM, IPPROTO_UDP, &dsmode, &fd));
      allocated_port1 =
          add_socket_to_server(s, fd, addr, rcv_buf_size, snd_buf_size);
      if (fd >= 0 && dsmode == GRPC_DSMODE_DUALSTACK) {
        if (port == 0) {
          /* This is the first time to bind to |addr|. If its port is still
           * wildcard port, update |addr| with the ephermeral port returned by
           * kernel. Thus |addr| can have a specific port in following
           * iterations. */
          grpc_sockaddr_set_port(addr, allocated_port1);
          port = allocated_port1;
        } else if (allocated_port1 >= 0) {
          /* The following successfully created socket should have same port as
           * the first one. */
          GPR_ASSERT(port == allocated_port1);
        }
        /* A dualstack socket is created, no need to create corresponding IPV4
         * socket. */
        continue;
      }

      /* If we didn't get a dualstack socket, also listen on 0.0.0.0. */
      if (port == 0 && allocated_port1 > 0) {
        /* |port| hasn't been assigned to an emphemeral port yet, |wild4| must
         * have a wildcard port. Update it with the emphemeral port created
         * during binding.*/
        grpc_sockaddr_set_port(&wild4, allocated_port1);
        port = allocated_port1;
      }
      /* |wild4| should have been updated with an emphemeral port by now. Use
       * this IPV4 address to create a IPV4 socket. */
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
        add_socket_to_server(s, fd, addr, rcv_buf_size, snd_buf_size);
    if (port == 0) {
      /* Update |addr| with the ephermeral port returned by kernel. So |addr|
       * can have a specific port in following iterations. */
      grpc_sockaddr_set_port(addr, allocated_port2);
      port = allocated_port2;
    } else if (allocated_port2 >= 0) {
      GPR_ASSERT(port == allocated_port2);
    }
  }

  gpr_free(allocated_addr);
  return port;
}

int grpc_udp_server_get_fd(grpc_udp_server* s, unsigned port_index) {
  if (port_index >= s->listeners.size()) {
    return -1;
  }

  return s->listeners[port_index].fd();
}

void grpc_udp_server_start(grpc_udp_server* udp_server,
                           const std::vector<grpc_pollset*>* pollsets,
                           void* user_data) {
  gpr_log(GPR_DEBUG, "grpc_udp_server_start");
  gpr_mu_lock(&udp_server->mu);
  GPR_ASSERT(udp_server->active_ports == 0);
  udp_server->pollsets = pollsets;
  udp_server->user_data = user_data;

  for (auto& listener : udp_server->listeners) {
    listener.StartListening(pollsets, udp_server->handler_factory);
  }

  gpr_mu_unlock(&udp_server->mu);
}

void GrpcUdpListener::StartListening(const std::vector<grpc_pollset*>* pollsets,
                                     GrpcUdpHandlerFactory* handler_factory) {
  gpr_mu_lock(&mutex_);
  handler_factory_ = handler_factory;
  udp_handler_ = handler_factory->CreateUdpHandler(emfd_, server_->user_data);
  for (grpc_pollset* pollset : *pollsets) {
    grpc_pollset_add_fd(pollset, emfd_);
  }
  GRPC_CLOSURE_INIT(&read_closure_, on_read, this, grpc_schedule_on_exec_ctx);
  grpc_fd_notify_on_read(emfd_, &read_closure_);

  GRPC_CLOSURE_INIT(&write_closure_, on_write, this, grpc_schedule_on_exec_ctx);
  notify_on_write_armed_ = true;
  grpc_fd_notify_on_write(emfd_, &write_closure_);

  /* Registered for both read and write callbacks: increment active_ports
   * twice to account for this, and delay free-ing of memory until both
   * on_read and on_write have fired. */
  server_->active_ports += 2;
  gpr_mu_unlock(&mutex_);
}

void GrpcUdpListener::OnDestroy() {
  if (udp_handler_ != nullptr) {
    handler_factory_->DestroyUdpHandler(udp_handler_);
  }
}

#endif
