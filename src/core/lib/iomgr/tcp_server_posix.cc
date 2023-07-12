//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include <utility>

#include <grpc/support/atm.h>

// FIXME: "posix" files shouldn't be depending on _GNU_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include <grpc/event_engine/event_engine.h>
#endif

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP_SERVER

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/byte_buffer.h>
#include <grpc/event_engine/endpoint_config.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/memory_allocator_factory.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/iomgr/event_engine_shims/closure.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/systemd_utils.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/tcp_server_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/transport/error_utils.h"

static std::atomic<int64_t> num_dropped_connections{0};
static constexpr grpc_core::Duration kRetryAcceptWaitTime{
    grpc_core::Duration::Seconds(1)};

using ::grpc_event_engine::experimental::EndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::MemoryAllocator;
using ::grpc_event_engine::experimental::MemoryQuotaBasedMemoryAllocatorFactory;
using ::grpc_event_engine::experimental::PosixEventEngineWithFdSupport;
using ::grpc_event_engine::experimental::SliceBuffer;

static grpc_error_handle CreateEventEngineListener(
    grpc_tcp_server* s, grpc_closure* shutdown_complete,
    const EndpointConfig& config, grpc_tcp_server** server) {
  absl::StatusOr<std::unique_ptr<EventEngine::Listener>> listener;
  if (grpc_event_engine::experimental::EventEngineSupportsFd()) {
    PosixEventEngineWithFdSupport::PosixAcceptCallback accept_cb =
        [s](int listener_fd, std::unique_ptr<EventEngine::Endpoint> ep,
            bool is_external, MemoryAllocator /*allocator*/,
            SliceBuffer* pending_data) {
          grpc_core::ApplicationCallbackExecCtx app_ctx;
          grpc_core::ExecCtx exec_ctx;
          grpc_tcp_server_acceptor* acceptor =
              static_cast<grpc_tcp_server_acceptor*>(
                  gpr_malloc(sizeof(*acceptor)));
          acceptor->from_server = s;
          acceptor->port_index = -1;
          acceptor->fd_index = -1;
          if (!is_external) {
            auto it = s->listen_fd_to_index_map.find(listener_fd);
            if (it != s->listen_fd_to_index_map.end()) {
              acceptor->port_index = std::get<0>(it->second);
              acceptor->fd_index = std::get<1>(it->second);
            }
          } else {
            // External connection handling.
            grpc_resolved_address addr;
            memset(&addr, 0, sizeof(addr));
            addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
            // Get the fd of the socket connected to peer.
            int fd = reinterpret_cast<grpc_event_engine::experimental::
                                          PosixEndpointWithFdSupport*>(ep.get())
                         ->GetWrappedFd();
            if (getpeername(fd, reinterpret_cast<struct sockaddr*>(addr.addr),
                            &(addr.len)) < 0) {
              gpr_log(GPR_ERROR, "Failed getpeername: %s",
                      grpc_core::StrError(errno).c_str());
              close(fd);
              return;
            }
            (void)grpc_set_socket_no_sigpipe_if_possible(fd);
            auto addr_uri = grpc_sockaddr_to_uri(&addr);
            if (!addr_uri.ok()) {
              gpr_log(GPR_ERROR, "Invalid address: %s",
                      addr_uri.status().ToString().c_str());
              return;
            }
            if (grpc_tcp_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "SERVER_CONNECT: incoming external connection: %s",
                      addr_uri->c_str());
            }
          }
          grpc_pollset* read_notifier_pollset =
              (*(s->pollsets))[static_cast<size_t>(gpr_atm_no_barrier_fetch_add(
                                   &s->next_pollset_to_assign, 1)) %
                               s->pollsets->size()];
          acceptor->external_connection = is_external;
          acceptor->listener_fd = listener_fd;
          grpc_byte_buffer* buf = nullptr;
          if (pending_data != nullptr && pending_data->Length() > 0) {
            buf = grpc_raw_byte_buffer_create(nullptr, 0);
            grpc_slice_buffer_swap(&buf->data.raw.slice_buffer,
                                   pending_data->c_slice_buffer());
            pending_data->Clear();
          }
          acceptor->pending_data = buf;
          s->on_accept_cb(s->on_accept_cb_arg,
                          grpc_event_engine::experimental::
                              grpc_event_engine_endpoint_create(std::move(ep)),
                          read_notifier_pollset, acceptor);
        };
    PosixEventEngineWithFdSupport* engine_ptr =
        reinterpret_cast<PosixEventEngineWithFdSupport*>(
            config.GetVoidPointer(GRPC_INTERNAL_ARG_EVENT_ENGINE));
    // Keeps the engine alive for some tests that have not otherwise
    // instantiated an EventEngine
    std::shared_ptr<EventEngine> keeper;
    if (engine_ptr == nullptr) {
      keeper = grpc_event_engine::experimental::GetDefaultEventEngine();
      engine_ptr =
          reinterpret_cast<PosixEventEngineWithFdSupport*>(keeper.get());
    }
    listener = engine_ptr->CreatePosixListener(
        std::move(accept_cb),
        [s, shutdown_complete](absl::Status status) {
          grpc_event_engine::experimental::RunEventEngineClosure(
              shutdown_complete, absl_status_to_grpc_error(status));
          delete s->fd_handler;
          delete s;
        },
        config,
        std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
            s->memory_quota));
  } else {
    EventEngine::Listener::AcceptCallback accept_cb =
        [s](std::unique_ptr<EventEngine::Endpoint> ep, MemoryAllocator) {
          s->on_accept_cb(s->on_accept_cb_arg,
                          grpc_event_engine::experimental::
                              grpc_event_engine_endpoint_create(std::move(ep)),
                          nullptr, nullptr);
        };
    auto ee = grpc_event_engine::experimental::GetDefaultEventEngine();
    listener = ee->CreateListener(
        std::move(accept_cb),
        [s, ee, shutdown_complete](absl::Status status) {
          GPR_ASSERT(gpr_atm_no_barrier_load(&s->refs.count) == 0);
          grpc_event_engine::experimental::RunEventEngineClosure(
              shutdown_complete, absl_status_to_grpc_error(status));
          delete s->fd_handler;
          delete s;
        },
        config,
        std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
            s->memory_quota));
  }
  if (!listener.ok()) {
    delete s;
    *server = nullptr;
    return listener.status();
  }
  s->ee_listener = std::move(*listener);
  return absl::OkStatus();
}

static grpc_error_handle tcp_server_create(grpc_closure* shutdown_complete,
                                           const EndpointConfig& config,
                                           grpc_tcp_server_cb on_accept_cb,
                                           void* on_accept_cb_arg,
                                           grpc_tcp_server** server) {
  grpc_tcp_server* s = new grpc_tcp_server;
  s->so_reuseport = grpc_is_socket_reuse_port_supported();
  s->expand_wildcard_addrs = false;
  auto value = config.GetInt(GRPC_ARG_ALLOW_REUSEPORT);
  if (value.has_value()) {
    s->so_reuseport = (grpc_is_socket_reuse_port_supported() && *value != 0);
  }
  value = config.GetInt(GRPC_ARG_EXPAND_WILDCARD_ADDRS);
  if (value.has_value()) {
    s->expand_wildcard_addrs = (*value != 0);
  }
  gpr_ref_init(&s->refs, 1);
  gpr_mu_init(&s->mu);
  s->active_ports = 0;
  s->destroyed_ports = 0;
  s->shutdown = false;
  s->shutdown_starting.head = nullptr;
  s->shutdown_starting.tail = nullptr;
  if (!grpc_event_engine::experimental::UseEventEngineListener()) {
    s->shutdown_complete = shutdown_complete;
  } else {
    s->shutdown_complete = nullptr;
  }
  s->on_accept_cb = on_accept_cb;
  s->on_accept_cb_arg = on_accept_cb_arg;
  s->head = nullptr;
  s->tail = nullptr;
  s->nports = 0;
  s->options = TcpOptionsFromEndpointConfig(config);
  s->fd_handler = nullptr;
  GPR_ASSERT(s->options.resource_quota != nullptr);
  GPR_ASSERT(s->on_accept_cb);
  s->memory_quota = s->options.resource_quota->memory_quota();
  s->pre_allocated_fd = -1;
  gpr_atm_no_barrier_store(&s->next_pollset_to_assign, 0);
  s->n_bind_ports = 0;
  new (&s->listen_fd_to_index_map)
      absl::flat_hash_map<int, std::tuple<int, int>>();
  *server = s;
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    return CreateEventEngineListener(s, shutdown_complete, config, server);
  }
  return absl::OkStatus();
}

static void finish_shutdown(grpc_tcp_server* s) {
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(s->shutdown);
  gpr_mu_unlock(&s->mu);
  if (s->shutdown_complete != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, s->shutdown_complete,
                            absl::OkStatus());
  }
  gpr_mu_destroy(&s->mu);
  while (s->head) {
    grpc_tcp_listener* sp = s->head;
    s->head = sp->next;
    gpr_free(sp);
  }
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    // This will trigger asynchronous execution of the on_shutdown_complete
    // callback when appropriate. That callback will delete the server
    s->ee_listener.reset();
  } else {
    delete s->fd_handler;
    delete s;
  }
}

static void destroyed_port(void* server, grpc_error_handle /*error*/) {
  grpc_tcp_server* s = static_cast<grpc_tcp_server*>(server);
  gpr_mu_lock(&s->mu);
  s->destroyed_ports++;
  if (s->destroyed_ports == s->nports) {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(s);
  } else {
    GPR_ASSERT(s->destroyed_ports < s->nports);
    gpr_mu_unlock(&s->mu);
  }
}

// called when all listening endpoints have been shutdown, so no further
// events will be received on them - at this point it's safe to destroy
// things
static void deactivated_all_ports(grpc_tcp_server* s) {
  // delete ALL the things
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(s->shutdown);

  if (s->head) {
    grpc_tcp_listener* sp;
    for (sp = s->head; sp; sp = sp->next) {
      // Do not unlink if there is a pre-allocated FD
      if (grpc_tcp_server_pre_allocated_fd(s) <= 0) {
        grpc_unlink_if_unix_domain_socket(&sp->addr);
      }
      GRPC_CLOSURE_INIT(&sp->destroyed_closure, destroyed_port, s,
                        grpc_schedule_on_exec_ctx);
      grpc_fd_orphan(sp->emfd, &sp->destroyed_closure, nullptr,
                     "tcp_listener_shutdown");
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(s);
  }
}

static void tcp_server_destroy(grpc_tcp_server* s) {
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->shutdown);
  s->shutdown = true;
  // shutdown all fd's
  if (s->active_ports) {
    grpc_tcp_listener* sp;
    for (sp = s->head; sp; sp = sp->next) {
      grpc_fd_shutdown(sp->emfd, GRPC_ERROR_CREATE("Server destroyed"));
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    deactivated_all_ports(s);
  }
}

// event manager callback when reads are ready
static void on_read(void* arg, grpc_error_handle err) {
  grpc_tcp_listener* sp = static_cast<grpc_tcp_listener*>(arg);
  grpc_pollset* read_notifier_pollset;
  if (!err.ok()) {
    goto error;
  }

  // loop until accept4 returns EAGAIN, and then re-arm notification
  for (;;) {
    grpc_resolved_address addr;
    memset(&addr, 0, sizeof(addr));
    addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
    // Note: If we ever decide to return this address to the user, remember to
    // strip off the ::ffff:0.0.0.0/96 prefix first.
    int fd = grpc_accept4(sp->fd, &addr, 1, 1);
    if (fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      // When the process runs out of fds, accept4() returns EMFILE. When this
      // happens, the connection is left in the accept queue until either a
      // read event triggers the on_read callback, or time has passed and the
      // accept should be re-tried regardless. This callback is not cancelled,
      // so a spurious wakeup may occur even when there's nothing to accept.
      // This is not a performant code path, but if an fd limit has been
      // reached, the system is likely in an unhappy state regardless.
      if (errno == EMFILE) {
        GRPC_LOG_EVERY_N_SEC(1, GPR_ERROR, "%s",
                             "File descriptor limit reached. Retrying.");
        grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
        if (gpr_atm_full_xchg(&sp->retry_timer_armed, true)) return;
        grpc_timer_init(&sp->retry_timer,
                        grpc_core::Timestamp::Now() + kRetryAcceptWaitTime,
                        &sp->retry_closure);
        return;
      }
      if (errno == EAGAIN || errno == ECONNABORTED || errno == EWOULDBLOCK) {
        grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
        return;
      }
      gpr_mu_lock(&sp->server->mu);
      if (!sp->server->shutdown_listeners) {
        gpr_log(GPR_ERROR, "Failed accept4: %s",
                grpc_core::StrError(errno).c_str());
      } else {
        // if we have shutdown listeners, accept4 could fail, and we
        // needn't notify users
      }
      gpr_mu_unlock(&sp->server->mu);
      goto error;
    }

    if (sp->server->memory_quota->IsMemoryPressureHigh()) {
      int64_t dropped_connections_count =
          num_dropped_connections.fetch_add(1, std::memory_order_relaxed) + 1;
      if (dropped_connections_count % 1000 == 1) {
        gpr_log(GPR_INFO,
                "Dropped >= %" PRId64
                " new connection attempts due to high memory pressure",
                dropped_connections_count);
      }
      close(fd);
      continue;
    }

    // For UNIX sockets, the accept call might not fill up the member sun_path
    // of sockaddr_un, so explicitly call getpeername to get it.
    if (grpc_is_unix_socket(&addr)) {
      memset(&addr, 0, sizeof(addr));
      addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
      if (getpeername(fd, reinterpret_cast<struct sockaddr*>(addr.addr),
                      &(addr.len)) < 0) {
        gpr_log(GPR_ERROR, "Failed getpeername: %s",
                grpc_core::StrError(errno).c_str());
        close(fd);
        goto error;
      }
    }

    (void)grpc_set_socket_no_sigpipe_if_possible(fd);

    err = grpc_apply_socket_mutator_in_args(fd, GRPC_FD_SERVER_CONNECTION_USAGE,
                                            sp->server->options);
    if (!err.ok()) {
      goto error;
    }

    auto addr_uri = grpc_sockaddr_to_uri(&addr);
    if (!addr_uri.ok()) {
      gpr_log(GPR_ERROR, "Invalid address: %s",
              addr_uri.status().ToString().c_str());
      goto error;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "SERVER_CONNECT: incoming connection: %s",
              addr_uri->c_str());
    }

    std::string name = absl::StrCat("tcp-server-connection:", addr_uri.value());
    grpc_fd* fdobj = grpc_fd_create(fd, name.c_str(), true);

    read_notifier_pollset = (*(sp->server->pollsets))
        [static_cast<size_t>(gpr_atm_no_barrier_fetch_add(
             &sp->server->next_pollset_to_assign, 1)) %
         sp->server->pollsets->size()];

    grpc_pollset_add_fd(read_notifier_pollset, fdobj);

    // Create acceptor.
    grpc_tcp_server_acceptor* acceptor =
        static_cast<grpc_tcp_server_acceptor*>(gpr_malloc(sizeof(*acceptor)));
    acceptor->from_server = sp->server;
    acceptor->port_index = sp->port_index;
    acceptor->fd_index = sp->fd_index;
    acceptor->external_connection = false;
    sp->server->on_accept_cb(
        sp->server->on_accept_cb_arg,
        grpc_tcp_create(fdobj, sp->server->options, addr_uri.value()),
        read_notifier_pollset, acceptor);
  }

  GPR_UNREACHABLE_CODE(return);

error:
  gpr_mu_lock(&sp->server->mu);
  if (0 == --sp->server->active_ports && sp->server->shutdown) {
    gpr_mu_unlock(&sp->server->mu);
    deactivated_all_ports(sp->server);
  } else {
    gpr_mu_unlock(&sp->server->mu);
  }
}

// Treat :: or 0.0.0.0 as a family-agnostic wildcard.
static grpc_error_handle add_wildcard_addrs_to_server(grpc_tcp_server* s,
                                                      unsigned port_index,
                                                      int requested_port,
                                                      int* out_port) {
  grpc_resolved_address wild4;
  grpc_resolved_address wild6;
  unsigned fd_index = 0;
  grpc_dualstack_mode dsmode;
  grpc_tcp_listener* sp = nullptr;
  grpc_tcp_listener* sp2 = nullptr;
  grpc_error_handle v6_err;
  grpc_error_handle v4_err;
  *out_port = -1;

  if (grpc_tcp_server_have_ifaddrs() && s->expand_wildcard_addrs) {
    return grpc_tcp_server_add_all_local_addrs(s, port_index, requested_port,
                                               out_port);
  }

  grpc_sockaddr_make_wildcards(requested_port, &wild4, &wild6);
  // Try listening on IPv6 first.
  if ((v6_err = grpc_tcp_server_add_addr(s, &wild6, port_index, fd_index,
                                         &dsmode, &sp)) == absl::OkStatus()) {
    ++fd_index;
    requested_port = *out_port = sp->port;
    if (dsmode == GRPC_DSMODE_DUALSTACK || dsmode == GRPC_DSMODE_IPV4) {
      return absl::OkStatus();
    }
  }
  // If we got a v6-only socket or nothing, try adding 0.0.0.0.
  grpc_sockaddr_set_port(&wild4, requested_port);
  if ((v4_err = grpc_tcp_server_add_addr(s, &wild4, port_index, fd_index,
                                         &dsmode, &sp2)) == absl::OkStatus()) {
    *out_port = sp2->port;
    if (sp != nullptr) {
      sp2->is_sibling = 1;
      sp->sibling = sp2;
    }
  }
  if (*out_port > 0) {
    if (!v6_err.ok()) {
      gpr_log(GPR_INFO,
              "Failed to add :: listener, "
              "the environment may not support IPv6: %s",
              grpc_core::StatusToString(v6_err).c_str());
    }
    if (!v4_err.ok()) {
      gpr_log(GPR_INFO,
              "Failed to add 0.0.0.0 listener, "
              "the environment may not support IPv4: %s",
              grpc_core::StatusToString(v4_err).c_str());
    }
    return absl::OkStatus();
  } else {
    grpc_error_handle root_err =
        GRPC_ERROR_CREATE("Failed to add any wildcard listeners");
    GPR_ASSERT(!v6_err.ok() && !v4_err.ok());
    root_err = grpc_error_add_child(root_err, v6_err);
    root_err = grpc_error_add_child(root_err, v4_err);
    return root_err;
  }
}

static grpc_error_handle clone_port(grpc_tcp_listener* listener,
                                    unsigned count) {
  grpc_tcp_listener* sp = nullptr;
  absl::StatusOr<std::string> addr_str;
  grpc_error_handle err;

  for (grpc_tcp_listener* l = listener->next; l && l->is_sibling; l = l->next) {
    l->fd_index += count;
  }

  for (unsigned i = 0; i < count; i++) {
    int fd = -1;
    int port = -1;
    grpc_dualstack_mode dsmode;
    err = grpc_create_dualstack_socket(&listener->addr, SOCK_STREAM, 0, &dsmode,
                                       &fd);
    if (!err.ok()) return err;
    err = grpc_tcp_server_prepare_socket(listener->server, fd, &listener->addr,
                                         true, &port);
    if (!err.ok()) return err;
    listener->server->nports++;
    addr_str = grpc_sockaddr_to_string(&listener->addr, true);
    if (!addr_str.ok()) {
      return GRPC_ERROR_CREATE(addr_str.status().ToString());
    }
    sp = static_cast<grpc_tcp_listener*>(gpr_malloc(sizeof(grpc_tcp_listener)));
    sp->next = listener->next;
    listener->next = sp;
    // sp (the new listener) is a sibling of 'listener' (the original
    // listener).
    sp->is_sibling = 1;
    sp->sibling = listener->sibling;
    listener->sibling = sp;
    sp->server = listener->server;
    sp->fd = fd;
    sp->emfd = grpc_fd_create(
        fd,
        absl::StrFormat("tcp-server-listener:%s/clone-%d", *addr_str, i)
            .c_str(),
        true);
    memcpy(&sp->addr, &listener->addr, sizeof(grpc_resolved_address));
    sp->port = port;
    sp->port_index = listener->port_index;
    sp->fd_index = listener->fd_index + count - i;
    GPR_ASSERT(sp->emfd);
    grpc_tcp_server_listener_initialize_retry_timer(sp);
    while (listener->server->tail->next != nullptr) {
      listener->server->tail = listener->server->tail->next;
    }
  }

  return absl::OkStatus();
}

static grpc_error_handle tcp_server_add_port(grpc_tcp_server* s,
                                             const grpc_resolved_address* addr,
                                             int* out_port) {
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    gpr_mu_lock(&s->mu);
    if (s->shutdown_listeners) {
      gpr_mu_unlock(&s->mu);
      return absl::UnknownError("Server already shutdown");
    }
    int fd_index = 0;
    absl::StatusOr<int> port;
    if (grpc_event_engine::experimental::EventEngineSupportsFd()) {
      port =
          static_cast<
              grpc_event_engine::experimental::PosixListenerWithFdSupport*>(
              s->ee_listener.get())
              ->BindWithFd(
                  grpc_event_engine::experimental::CreateResolvedAddress(*addr),
                  [s, &fd_index](absl::StatusOr<int> listen_fd) {
                    if (!listen_fd.ok()) {
                      return;
                    }
                    GPR_DEBUG_ASSERT(*listen_fd > 0);
                    s->listen_fd_to_index_map.insert_or_assign(
                        *listen_fd,
                        std::make_tuple(s->n_bind_ports, fd_index++));
                  });
    } else {
      port = s->ee_listener->Bind(
          grpc_event_engine::experimental::CreateResolvedAddress(*addr));
    }
    if (port.ok()) {
      s->n_bind_ports++;
      *out_port = *port;
    }
    gpr_mu_unlock(&s->mu);
    return port.status();
  }
  GPR_ASSERT(addr->len <= GRPC_MAX_SOCKADDR_SIZE);
  grpc_tcp_listener* sp;
  grpc_resolved_address sockname_temp;
  grpc_resolved_address addr6_v4mapped;
  int requested_port = grpc_sockaddr_get_port(addr);
  unsigned port_index = 0;
  grpc_dualstack_mode dsmode;
  grpc_error_handle err;
  *out_port = -1;
  if (s->tail != nullptr) {
    port_index = s->tail->port_index + 1;
  }

  // Check if this is a wildcard port, and if so, try to keep the port the same
  // as some previously created listener.
  if (requested_port == 0) {
    for (sp = s->head; sp; sp = sp->next) {
      sockname_temp.len =
          static_cast<socklen_t>(sizeof(struct sockaddr_storage));
      if (0 ==
          getsockname(sp->fd,
                      reinterpret_cast<grpc_sockaddr*>(&sockname_temp.addr),
                      &sockname_temp.len)) {
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

  /* Check if systemd has pre-allocated valid FDs */
  set_matching_sd_fds(s, addr, requested_port);

  /* Do not unlink if there are pre-allocated FDs, or it will stop
     working after the first client connects */
  if (grpc_tcp_server_pre_allocated_fd(s) <= 0) {
    grpc_unlink_if_unix_domain_socket(addr);
  }

  if (grpc_sockaddr_is_wildcard(addr, &requested_port)) {
    return add_wildcard_addrs_to_server(s, port_index, requested_port,
                                        out_port);
  }
  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }
  if ((err = grpc_tcp_server_add_addr(s, addr, port_index, 0, &dsmode, &sp)) ==
      absl::OkStatus()) {
    *out_port = sp->port;
  }
  return err;
}

// Return listener at port_index or NULL. Should only be called with s->mu
// locked.
static grpc_tcp_listener* get_port_index(grpc_tcp_server* s,
                                         unsigned port_index) {
  unsigned num_ports = 0;
  grpc_tcp_listener* sp;
  for (sp = s->head; sp; sp = sp->next) {
    if (!sp->is_sibling) {
      if (++num_ports > port_index) {
        return sp;
      }
    }
  }
  return nullptr;
}

unsigned tcp_server_port_fd_count(grpc_tcp_server* s, unsigned port_index) {
  unsigned num_fds = 0;
  gpr_mu_lock(&s->mu);
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    // This doesn't need to be very fast. Used in tests.
    for (auto it = s->listen_fd_to_index_map.begin();
         it != s->listen_fd_to_index_map.end(); it++) {
      if (std::get<0>(it->second) == static_cast<int>(port_index)) {
        num_fds++;
      }
    }
    gpr_mu_unlock(&s->mu);
    return num_fds;
  }
  grpc_tcp_listener* sp = get_port_index(s, port_index);
  for (; sp; sp = sp->sibling) {
    ++num_fds;
  }
  gpr_mu_unlock(&s->mu);
  return num_fds;
}

static int tcp_server_port_fd(grpc_tcp_server* s, unsigned port_index,
                              unsigned fd_index) {
  gpr_mu_lock(&s->mu);
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    // This doesn't need to be very fast. Used in tests.
    for (auto it = s->listen_fd_to_index_map.begin();
         it != s->listen_fd_to_index_map.end(); it++) {
      if (std::get<0>(it->second) == static_cast<int>(port_index) &&
          std::get<1>(it->second) == static_cast<int>(fd_index)) {
        gpr_mu_unlock(&s->mu);
        return it->first;
      }
    }
    gpr_mu_unlock(&s->mu);
    return -1;
  }
  grpc_tcp_listener* sp = get_port_index(s, port_index);
  for (; sp; sp = sp->sibling, --fd_index) {
    if (fd_index == 0) {
      gpr_mu_unlock(&s->mu);
      return sp->fd;
    }
  }
  gpr_mu_unlock(&s->mu);
  return -1;
}

static void tcp_server_start(grpc_tcp_server* s,
                             const std::vector<grpc_pollset*>* pollsets) {
  size_t i;
  grpc_tcp_listener* sp;
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(s->on_accept_cb);
  GPR_ASSERT(s->active_ports == 0);
  s->pollsets = pollsets;
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    GPR_ASSERT(!s->shutdown_listeners);
    GPR_ASSERT(GRPC_LOG_IF_ERROR("listener_start", s->ee_listener->Start()));
    gpr_mu_unlock(&s->mu);
    return;
  }
  sp = s->head;
  while (sp != nullptr) {
    if (s->so_reuseport && !grpc_is_unix_socket(&sp->addr) &&
        pollsets->size() > 1) {
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "clone_port", clone_port(sp, (unsigned)(pollsets->size() - 1))));
      for (i = 0; i < pollsets->size(); i++) {
        grpc_pollset_add_fd((*pollsets)[i], sp->emfd);
        GRPC_CLOSURE_INIT(&sp->read_closure, on_read, sp,
                          grpc_schedule_on_exec_ctx);
        grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
        s->active_ports++;
        sp = sp->next;
      }
    } else {
      for (i = 0; i < pollsets->size(); i++) {
        grpc_pollset_add_fd((*pollsets)[i], sp->emfd);
      }
      GRPC_CLOSURE_INIT(&sp->read_closure, on_read, sp,
                        grpc_schedule_on_exec_ctx);
      grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
      s->active_ports++;
      sp = sp->next;
    }
  }
  gpr_mu_unlock(&s->mu);
}

grpc_tcp_server* tcp_server_ref(grpc_tcp_server* s) {
  gpr_ref_non_zero(&s->refs);
  return s;
}

static void tcp_server_shutdown_starting_add(grpc_tcp_server* s,
                                             grpc_closure* shutdown_starting) {
  gpr_mu_lock(&s->mu);
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting,
                           absl::OkStatus());
  gpr_mu_unlock(&s->mu);
}

static void tcp_server_unref(grpc_tcp_server* s) {
  if (gpr_unref(&s->refs)) {
    grpc_tcp_server_shutdown_listeners(s);
    gpr_mu_lock(&s->mu);
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &s->shutdown_starting);
    gpr_mu_unlock(&s->mu);
    tcp_server_destroy(s);
  }
}

static void tcp_server_shutdown_listeners(grpc_tcp_server* s) {
  gpr_mu_lock(&s->mu);
  s->shutdown_listeners = true;
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    if (grpc_event_engine::experimental::EventEngineSupportsFd()) {
      static_cast<grpc_event_engine::experimental::PosixListenerWithFdSupport*>(
          s->ee_listener.get())
          ->ShutdownListeningFds();
    }
  }
  /* shutdown all fd's */
  if (s->active_ports) {
    grpc_tcp_listener* sp;
    for (sp = s->head; sp; sp = sp->next) {
      grpc_timer_cancel(&sp->retry_timer);
      grpc_fd_shutdown(sp->emfd, GRPC_ERROR_CREATE("Server shutdown"));
    }
  }
  gpr_mu_unlock(&s->mu);
}

static int tcp_server_pre_allocated_fd(grpc_tcp_server* s) {
  return s->pre_allocated_fd;
}

static void tcp_server_set_pre_allocated_fd(grpc_tcp_server* s, int fd) {
  gpr_mu_lock(&s->mu);
  s->pre_allocated_fd = fd;
  gpr_mu_unlock(&s->mu);
}

namespace {
class ExternalConnectionHandler : public grpc_core::TcpServerFdHandler {
 public:
  explicit ExternalConnectionHandler(grpc_tcp_server* s) : s_(s) {}

  // TODO(yangg) resolve duplicate code with on_read
  void Handle(int listener_fd, int fd, grpc_byte_buffer* buf) override {
    if (grpc_event_engine::experimental::UseEventEngineListener()) {
      GPR_ASSERT(grpc_event_engine::experimental::EventEngineSupportsFd());
      grpc_event_engine::experimental::SliceBuffer pending_data;
      if (buf != nullptr) {
        pending_data =
            grpc_event_engine::experimental::SliceBuffer::TakeCSliceBuffer(
                buf->data.raw.slice_buffer);
      }
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "listener_handle_external_connection",
          static_cast<
              grpc_event_engine::experimental::PosixListenerWithFdSupport*>(
              s_->ee_listener.get())
              ->HandleExternalConnection(listener_fd, fd, &pending_data)));
      return;
    }
    grpc_pollset* read_notifier_pollset;
    grpc_resolved_address addr;
    memset(&addr, 0, sizeof(addr));
    addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
    grpc_core::ExecCtx exec_ctx;

    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(addr.addr),
                    &(addr.len)) < 0) {
      gpr_log(GPR_ERROR, "Failed getpeername: %s",
              grpc_core::StrError(errno).c_str());
      close(fd);
      return;
    }
    (void)grpc_set_socket_no_sigpipe_if_possible(fd);
    auto addr_uri = grpc_sockaddr_to_uri(&addr);
    if (!addr_uri.ok()) {
      gpr_log(GPR_ERROR, "Invalid address: %s",
              addr_uri.status().ToString().c_str());
      return;
    }
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "SERVER_CONNECT: incoming external connection: %s",
              addr_uri->c_str());
    }
    std::string name = absl::StrCat("tcp-server-connection:", addr_uri.value());
    grpc_fd* fdobj = grpc_fd_create(fd, name.c_str(), true);
    read_notifier_pollset =
        (*(s_->pollsets))[static_cast<size_t>(gpr_atm_no_barrier_fetch_add(
                              &s_->next_pollset_to_assign, 1)) %
                          s_->pollsets->size()];
    grpc_pollset_add_fd(read_notifier_pollset, fdobj);
    grpc_tcp_server_acceptor* acceptor =
        static_cast<grpc_tcp_server_acceptor*>(gpr_malloc(sizeof(*acceptor)));
    acceptor->from_server = s_;
    acceptor->port_index = -1;
    acceptor->fd_index = -1;
    acceptor->external_connection = true;
    acceptor->listener_fd = listener_fd;
    acceptor->pending_data = buf;
    s_->on_accept_cb(s_->on_accept_cb_arg,
                     grpc_tcp_create(fdobj, s_->options, addr_uri.value()),
                     read_notifier_pollset, acceptor);
  }

 private:
  grpc_tcp_server* s_;
};
}  // namespace

static grpc_core::TcpServerFdHandler* tcp_server_create_fd_handler(
    grpc_tcp_server* s) {
  s->fd_handler = new ExternalConnectionHandler(s);
  return s->fd_handler;
}

grpc_tcp_server_vtable grpc_posix_tcp_server_vtable = {
    tcp_server_create,
    tcp_server_start,
    tcp_server_add_port,
    tcp_server_create_fd_handler,
    tcp_server_port_fd_count,
    tcp_server_port_fd,
    tcp_server_ref,
    tcp_server_shutdown_starting_add,
    tcp_server_unref,
    tcp_server_shutdown_listeners,
    tcp_server_pre_allocated_fd,
    tcp_server_set_pre_allocated_fd};

#endif  // GRPC_POSIX_SOCKET_TCP_SERVER
