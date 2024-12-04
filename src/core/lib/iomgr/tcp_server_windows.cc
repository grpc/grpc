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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <io.h>

#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/memory_allocator_factory.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/event_engine/windows/windows_listener.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/event_engine_shims/closure.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/crash.h"

#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

namespace {
using ::grpc_event_engine::experimental::CreateResolvedAddress;
using ::grpc_event_engine::experimental::EndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::grpc_event_engine_endpoint_create;
using ::grpc_event_engine::experimental::MemoryAllocator;
using ::grpc_event_engine::experimental::MemoryQuotaBasedMemoryAllocatorFactory;
using ::grpc_event_engine::experimental::ResolvedAddressSetPort;
using ::grpc_event_engine::experimental::RunEventEngineClosure;
using ::grpc_event_engine::experimental::WindowsEventEngine;
using ::grpc_event_engine::experimental::WindowsEventEngineListener;
}  // namespace

// one listening port
typedef struct grpc_tcp_listener grpc_tcp_listener;
struct grpc_tcp_listener {
  // Buffer to hold the local and remote address.
  // This seemingly magic number comes from AcceptEx's documentation. each
  // address buffer needs to have at least 16 more bytes at their end.
#ifdef GRPC_HAVE_UNIX_SOCKET
  // unix addr is larger than ip addr.
  uint8_t addresses[(sizeof(sockaddr_un) + 16) * 2] = {};
#else
  uint8_t addresses[(sizeof(grpc_sockaddr_in6) + 16) * 2];
#endif  // GRPC_HAVE_UNIX_SOCKET
  // This will hold the socket for the next accept.
  SOCKET new_socket;
  // The listener winsocket.
  grpc_winsocket* socket;
  // address of listener
  grpc_resolved_address resolved_addr;
  // The actual TCP port number.
  int port;
  unsigned port_index;
  grpc_tcp_server* server;
  // The cached AcceptEx for that port.
  LPFN_ACCEPTEX AcceptEx;
  int shutting_down;
  int outstanding_calls;
  // closure for socket notification of accept being ready
  grpc_closure on_accept;
  // linked list
  struct grpc_tcp_listener* next;
};

// the overall server
struct grpc_tcp_server {
  gpr_refcount refs;
  // Called whenever accept() succeeds on a server port.
  grpc_tcp_server_cb on_accept_cb;
  void* on_accept_cb_arg;

  gpr_mu mu;

  // active port count: how many ports are actually still listening
  int active_ports;

  // linked list of server ports
  grpc_tcp_listener* head;
  grpc_tcp_listener* tail;

  // List of closures passed to shutdown_starting_add().
  grpc_closure_list shutdown_starting;

  // shutdown callback
  grpc_closure* shutdown_complete;

  // used for the EventEngine shim
  WindowsEventEngineListener* ee_listener;
};

// TODO(hork): This may be refactored to share with posix engine and event
// engine.
void unlink_if_unix_domain_socket(const grpc_resolved_address* resolved_addr) {
#ifdef GRPC_HAVE_UNIX_SOCKET
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family != AF_UNIX) {
    return;
  }
  struct sockaddr_un* un =
      reinterpret_cast<struct sockaddr_un*>(const_cast<sockaddr*>(addr));
  // There is nothing to unlink for an abstract unix socket.
  if (un->sun_path[0] == '\0' && un->sun_path[1] != '\0') {
    return;
  }
  // For windows we need to remove the file instead of unlink.
  DWORD attr = ::GetFileAttributesA(un->sun_path);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return;
  }
  if (attr & FILE_ATTRIBUTE_DIRECTORY || attr & FILE_ATTRIBUTE_READONLY) {
    return;
  }
  ::DeleteFileA(un->sun_path);
#else
  (void)resolved_addr;
#endif
}

// Public function. Allocates the proper data structures to hold a
// grpc_tcp_server.
static grpc_error_handle tcp_server_create(grpc_closure* shutdown_complete,
                                           const EndpointConfig& /* config */,
                                           grpc_tcp_server_cb on_accept_cb,
                                           void* on_accept_cb_arg,
                                           grpc_tcp_server** server) {
  grpc_tcp_server* s = (grpc_tcp_server*)gpr_malloc(sizeof(grpc_tcp_server));
  gpr_ref_init(&s->refs, 1);
  gpr_mu_init(&s->mu);
  s->active_ports = 0;
  s->on_accept_cb = on_accept_cb;
  s->on_accept_cb_arg = on_accept_cb_arg;
  s->head = NULL;
  s->tail = NULL;
  s->shutdown_starting.head = NULL;
  s->shutdown_starting.tail = NULL;
  s->shutdown_complete = shutdown_complete;
  *server = s;
  return absl::OkStatus();
}

static void destroy_server(void* arg, grpc_error_handle /* error */) {
  grpc_tcp_server* s = (grpc_tcp_server*)arg;

  // Now that the accepts have been aborted, we can destroy the sockets.
  // The IOCP won't get notified on these, so we can flag them as already
  // closed by the system.
  while (s->head) {
    grpc_tcp_listener* sp = s->head;
    s->head = sp->next;
    sp->next = NULL;
    grpc_winsocket_destroy(sp->socket);
    unlink_if_unix_domain_socket(&sp->resolved_addr);
    gpr_free(sp);
  }
  gpr_mu_destroy(&s->mu);
  gpr_free(s);
}

static void finish_shutdown_locked(grpc_tcp_server* s) {
  if (s->shutdown_complete != NULL) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, s->shutdown_complete,
                            absl::OkStatus());
  }

  grpc_core::ExecCtx::Run(
      DEBUG_LOCATION,
      GRPC_CLOSURE_CREATE(destroy_server, s, grpc_schedule_on_exec_ctx),
      absl::OkStatus());
}

static grpc_tcp_server* tcp_server_ref(grpc_tcp_server* s) {
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

static void tcp_server_destroy(grpc_tcp_server* s) {
  grpc_tcp_listener* sp;
  gpr_mu_lock(&s->mu);
  // First, shutdown all fd's. This will queue abortion calls for all
  // of the pending accepts due to the normal operation mechanism.
  if (s->active_ports == 0) {
    finish_shutdown_locked(s);
  } else {
    for (sp = s->head; sp; sp = sp->next) {
      sp->shutting_down = 1;
      grpc_winsocket_shutdown(sp->socket);
    }
  }
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

// Prepare (bind) a recently-created socket for listening.
static grpc_error_handle prepare_socket(SOCKET sock,
                                        const grpc_resolved_address* addr,
                                        int* port) {
  grpc_resolved_address sockname_temp;
  grpc_error_handle error;
  int sockname_temp_len;
  if (grpc_sockaddr_get_family(addr) == AF_UNIX) {
    error = grpc_tcp_set_non_block(sock);
  } else {
    error = grpc_tcp_prepare_socket(sock);
  }
  if (!error.ok()) {
    goto failure;
  }
  unlink_if_unix_domain_socket(addr);
  if (bind(sock, (const grpc_sockaddr*)addr->addr, (int)addr->len) ==
      SOCKET_ERROR) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "bind");
    goto failure;
  }

  if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "listen");
    goto failure;
  }

  sockname_temp_len = sizeof(struct sockaddr_storage);
  if (getsockname(sock, (grpc_sockaddr*)sockname_temp.addr,
                  &sockname_temp_len) == SOCKET_ERROR) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "getsockname");
    goto failure;
  }
  sockname_temp.len = (size_t)sockname_temp_len;

  *port = grpc_sockaddr_get_port(&sockname_temp);
  return absl::OkStatus();

failure:
  CHECK(!error.ok());
  error = grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING(
                                 "Failed to prepare server socket", &error, 1),
                             grpc_core::StatusIntProperty::kFd, (intptr_t)sock);
  if (sock != INVALID_SOCKET) closesocket(sock);
  return error;
}

static void decrement_active_ports_and_notify_locked(grpc_tcp_listener* sp) {
  sp->shutting_down = 0;
  CHECK_GT(sp->server->active_ports, 0u);
  if (0 == --sp->server->active_ports) {
    finish_shutdown_locked(sp->server);
  }
}

// In order to do an async accept, we need to create a socket first which
// will be the one assigned to the new incoming connection.
static grpc_error_handle start_accept_locked(grpc_tcp_listener* port) {
  SOCKET sock = INVALID_SOCKET;
  BOOL success;
  const DWORD addrlen = sizeof(port->addresses) / 2;
  DWORD bytes_received = 0;
  grpc_error_handle error;

  if (port->shutting_down) {
    return absl::OkStatus();
  }
  const int addr_family =
      grpc_sockaddr_get_family(&port->resolved_addr) == AF_UNIX ? AF_UNIX
                                                                : AF_INET6;
  const int protocol = addr_family == AF_UNIX ? 0 : IPPROTO_TCP;
  sock = WSASocket(addr_family, SOCK_STREAM, protocol, NULL, 0,
                   grpc_get_default_wsa_socket_flags());
  if (sock == INVALID_SOCKET) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
    goto failure;
  }
  if (addr_family == AF_UNIX) {
    error = grpc_tcp_set_non_block(sock);
  } else {
    error = grpc_tcp_prepare_socket(sock);
  }
  if (!error.ok()) goto failure;

  // Start the "accept" asynchronously.
  success = port->AcceptEx(port->socket->socket, sock, port->addresses, 0,
                           addrlen, addrlen, &bytes_received,
                           &port->socket->read_info.overlapped);

  // It is possible to get an accept immediately without delay. However, we
  // will still get an IOCP notification for it. So let's just ignore it.
  if (!success) {
    int last_error = WSAGetLastError();
    if (last_error != ERROR_IO_PENDING) {
      error = GRPC_WSA_ERROR(last_error, "AcceptEx");
      goto failure;
    }
  }

  // We're ready to do the accept. Calling grpc_socket_notify_on_read may
  // immediately process an accept that happened in the meantime.
  port->new_socket = sock;
  grpc_socket_notify_on_read(port->socket, &port->on_accept);
  port->outstanding_calls++;
  return error;

failure:
  CHECK(!error.ok());
  if (sock != INVALID_SOCKET) closesocket(sock);
  return error;
}

// Event manager callback when reads are ready.
static void on_accept(void* arg, grpc_error_handle error) {
  grpc_tcp_listener* sp = (grpc_tcp_listener*)arg;
  SOCKET sock = sp->new_socket;
  grpc_winsocket_callback_info* info = &sp->socket->read_info;
  grpc_endpoint* ep = NULL;
  grpc_resolved_address peer_name;
  DWORD transferred_bytes;
  DWORD flags;
  BOOL wsa_success;
  int err;

  gpr_mu_lock(&sp->server->mu);

  peer_name.len = sizeof(struct sockaddr_storage);

  // The general mechanism for shutting down is to queue abortion calls. While
  // this is necessary in the read/write case, it's useless for the accept
  // case. We only need to adjust the pending callback count
  if (!error.ok()) {
    VLOG(2) << "Skipping on_accept due to error: "
            << grpc_core::StatusToString(error);

    gpr_mu_unlock(&sp->server->mu);
    return;
  }
  // The IOCP notified us of a completed operation. Let's grab the results,
  // and act accordingly.
  transferred_bytes = 0;
  wsa_success = WSAGetOverlappedResult(sock, &info->overlapped,
                                       &transferred_bytes, FALSE, &flags);
  if (!wsa_success) {
    if (!sp->shutting_down) {
      char* utf8_message = gpr_format_message(WSAGetLastError());
      LOG(ERROR) << "on_accept error: " << utf8_message;
      gpr_free(utf8_message);
    }
    closesocket(sock);
  } else {
    if (!sp->shutting_down) {
      err = setsockopt(sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       (char*)&sp->socket->socket, sizeof(sp->socket->socket));
      if (err) {
        char* utf8_message = gpr_format_message(WSAGetLastError());
        LOG(ERROR) << "setsockopt error: " << utf8_message;
        gpr_free(utf8_message);
      }
      int peer_name_len = (int)peer_name.len;
      err = getpeername(sock, (grpc_sockaddr*)peer_name.addr, &peer_name_len);
      peer_name.len = (size_t)peer_name_len;
      std::string peer_name_string;
      if (!err) {
        auto addr_uri = grpc_sockaddr_to_uri(&peer_name);
        if (addr_uri.ok()) {
          peer_name_string = addr_uri.value();
        } else {
          LOG(ERROR) << "invalid peer name: " << addr_uri.status();
        }
      } else {
        char* utf8_message = gpr_format_message(WSAGetLastError());
        LOG(ERROR) << "getpeername error: " << utf8_message;
        gpr_free(utf8_message);
      }
      std::string fd_name = absl::StrCat("tcp_server:", peer_name_string);
      ep = grpc_tcp_create(grpc_winsocket_create(sock, fd_name.c_str()),
                           peer_name_string);
    } else {
      closesocket(sock);
    }
  }

  // The only time we should call our callback, is where we successfully
  // managed to accept a connection, and created an endpoint.
  if (ep) {
    // Create acceptor.
    grpc_tcp_server_acceptor* acceptor =
        (grpc_tcp_server_acceptor*)gpr_malloc(sizeof(*acceptor));
    acceptor->from_server = sp->server;
    acceptor->port_index = sp->port_index;
    acceptor->fd_index = 0;
    acceptor->external_connection = false;
    sp->server->on_accept_cb(sp->server->on_accept_cb_arg, ep, NULL, acceptor);
  }
  // As we were notified from the IOCP of one and exactly one accept,
  // the former socked we created has now either been destroy or assigned
  // to the new connection. We need to create a new one for the next
  // connection.
  CHECK(GRPC_LOG_IF_ERROR("start_accept", start_accept_locked(sp)));
  if (0 == --sp->outstanding_calls) {
    decrement_active_ports_and_notify_locked(sp);
  }
  gpr_mu_unlock(&sp->server->mu);
}

static grpc_error_handle add_socket_to_server(grpc_tcp_server* s, SOCKET sock,
                                              const grpc_resolved_address* addr,
                                              unsigned port_index,
                                              grpc_tcp_listener** listener) {
  grpc_tcp_listener* sp = NULL;
  int port = -1;
  int status;
  GUID guid = WSAID_ACCEPTEX;
  DWORD ioctl_num_bytes;
  LPFN_ACCEPTEX AcceptEx;
  grpc_error_handle error;

  // We need to grab the AcceptEx pointer for that port, as it may be
  // interface-dependent. We'll cache it to avoid doing that again.
  status =
      WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &AcceptEx, sizeof(AcceptEx), &ioctl_num_bytes, NULL, NULL);

  if (status != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "AcceptEx pointer retrieval");
    closesocket(sock);
    return error;
  }

  error = prepare_socket(sock, addr, &port);
  if (!error.ok()) {
    return error;
  }

  CHECK_GE(port, 0);
  gpr_mu_lock(&s->mu);
  sp = (grpc_tcp_listener*)gpr_malloc(sizeof(grpc_tcp_listener));
  sp->next = NULL;
  if (s->head == NULL) {
    s->head = sp;
  } else {
    s->tail->next = sp;
  }
  s->tail = sp;
  sp->server = s;
  sp->socket = grpc_winsocket_create(sock, "listener");
  sp->shutting_down = 0;
  sp->outstanding_calls = 0;
  sp->AcceptEx = AcceptEx;
  sp->new_socket = INVALID_SOCKET;
  sp->resolved_addr = *addr;
  sp->port = port;
  sp->port_index = port_index;
  GRPC_CLOSURE_INIT(&sp->on_accept, on_accept, sp, grpc_schedule_on_exec_ctx);
  CHECK(sp->socket);
  gpr_mu_unlock(&s->mu);
  *listener = sp;

  return absl::OkStatus();
}

static grpc_error_handle tcp_server_add_port(grpc_tcp_server* s,
                                             const grpc_resolved_address* addr,
                                             int* port) {
  grpc_tcp_listener* sp = NULL;
  SOCKET sock;
  grpc_resolved_address addr6_v4mapped;
  grpc_resolved_address wildcard;
  grpc_resolved_address* allocated_addr = NULL;
  unsigned port_index = 0;
  grpc_error_handle error;

  if (s->tail != NULL) {
    port_index = s->tail->port_index + 1;
  }

  // Check if this is a wildcard port, and if so, try to keep the port the same
  // as some previously created listener.
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (sp = s->head; sp; sp = sp->next) {
      grpc_resolved_address sockname_temp;
      int sockname_temp_len = sizeof(struct sockaddr_storage);
      if (0 == getsockname(sp->socket->socket,
                           (grpc_sockaddr*)sockname_temp.addr,
                           &sockname_temp_len)) {
        sockname_temp.len = (size_t)sockname_temp_len;
        *port = grpc_sockaddr_get_port(&sockname_temp);
        if (*port > 0) {
          allocated_addr =
              (grpc_resolved_address*)gpr_malloc(sizeof(grpc_resolved_address));
          memcpy(allocated_addr, addr, sizeof(grpc_resolved_address));
          grpc_sockaddr_set_port(allocated_addr, *port);
          addr = allocated_addr;
          break;
        }
      }
    }
  }

  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }

  // Treat :: or 0.0.0.0 as a family-agnostic wildcard.
  if (grpc_sockaddr_is_wildcard(addr, port)) {
    grpc_sockaddr_make_wildcard6(*port, &wildcard);

    addr = &wildcard;
  }

  const int addr_family =
      grpc_sockaddr_get_family(addr) == AF_UNIX ? AF_UNIX : AF_INET6;
  const int protocol = addr_family == AF_UNIX ? 0 : IPPROTO_TCP;
  sock = WSASocket(addr_family, SOCK_STREAM, protocol, NULL, 0,
                   grpc_get_default_wsa_socket_flags());
  if (sock == INVALID_SOCKET) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
    goto done;
  }

  error = add_socket_to_server(s, sock, addr, port_index, &sp);

done:
  gpr_free(allocated_addr);

  if (!error.ok()) {
    grpc_error_handle error_out = GRPC_ERROR_CREATE_REFERENCING(
        "Failed to add port to server", &error, 1);
    error = error_out;
    *port = -1;
  } else {
    CHECK(sp != NULL);
    *port = sp->port;
  }
  return error;
}

static void tcp_server_start(grpc_tcp_server* s,
                             const std::vector<grpc_pollset*>* /*pollsets*/) {
  grpc_tcp_listener* sp;
  gpr_mu_lock(&s->mu);
  CHECK_EQ(s->active_ports, 0u);
  for (sp = s->head; sp; sp = sp->next) {
    CHECK(GRPC_LOG_IF_ERROR("start_accept", start_accept_locked(sp)));
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);
}

static unsigned tcp_server_port_fd_count(grpc_tcp_server* /* s */,
                                         unsigned /* port_index */) {
  return 0;
}

static int tcp_server_port_fd(grpc_tcp_server* /* s */,
                              unsigned /* port_index */,
                              unsigned /* fd_index */) {
  return -1;
}

static grpc_core::TcpServerFdHandler* tcp_server_create_fd_handler(
    grpc_tcp_server* /* s */) {
  return nullptr;
}

static void tcp_server_shutdown_listeners(grpc_tcp_server* /* s */) {}

static int tcp_pre_allocated_fd(grpc_tcp_server* /* s */) { return -1; }

static void tcp_set_pre_allocated_fd(grpc_tcp_server* /* s */, int /* fd */) {}

grpc_tcp_server_vtable grpc_windows_tcp_server_vtable = {
    tcp_server_create,        tcp_server_start,
    tcp_server_add_port,      tcp_server_create_fd_handler,
    tcp_server_port_fd_count, tcp_server_port_fd,
    tcp_server_ref,           tcp_server_shutdown_starting_add,
    tcp_server_unref,         tcp_server_shutdown_listeners,
    tcp_pre_allocated_fd,     tcp_set_pre_allocated_fd};

// ---- EventEngine shim ------------------------------------------------------

namespace {

static grpc_error_handle event_engine_create(grpc_closure* shutdown_complete,
                                             const EndpointConfig& config,
                                             grpc_tcp_server_cb on_accept_cb,
                                             void* on_accept_cb_arg,
                                             grpc_tcp_server** server) {
  // On Windows, the event_engine_listener experiment only supports the
  // default engine
  WindowsEventEngine* engine_ptr = reinterpret_cast<WindowsEventEngine*>(
      config.GetVoidPointer(GRPC_INTERNAL_ARG_EVENT_ENGINE));
  grpc_tcp_server* s = (grpc_tcp_server*)gpr_malloc(sizeof(grpc_tcp_server));
  CHECK_NE(on_accept_cb, nullptr);
  auto accept_cb = [s, on_accept_cb, on_accept_cb_arg](
                       std::unique_ptr<EventEngine::Endpoint> endpoint,
                       MemoryAllocator memory_allocator) {
    grpc_core::ApplicationCallbackExecCtx app_ctx;
    grpc_core::ExecCtx exec_ctx;
    grpc_tcp_server_acceptor* acceptor =
        static_cast<grpc_tcp_server_acceptor*>(gpr_malloc(sizeof(*acceptor)));
    acceptor->from_server = s;
    acceptor->port_index = -1;
    acceptor->fd_index = -1;
    acceptor->external_connection = false;
    on_accept_cb(on_accept_cb_arg,
                 grpc_event_engine_endpoint_create(std::move(endpoint)),
                 nullptr, acceptor);
  };
  auto on_shutdown = [shutdown_complete](absl::Status status) {
    RunEventEngineClosure(shutdown_complete, status);
  };
  grpc_core::RefCountedPtr<grpc_core::ResourceQuota> resource_quota;
  {
    void* tmp_quota = config.GetVoidPointer(GRPC_ARG_RESOURCE_QUOTA);
    CHECK_NE(tmp_quota, nullptr);
    resource_quota =
        reinterpret_cast<grpc_core::ResourceQuota*>(tmp_quota)->Ref();
  }
  gpr_ref_init(&s->refs, 1);
  gpr_mu_init(&s->mu);
  s->ee_listener = new WindowsEventEngineListener(
      engine_ptr->poller(), std::move(accept_cb), std::move(on_shutdown),
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          resource_quota->memory_quota()),
      engine_ptr->shared_from_this(), engine_ptr->thread_pool(), config);
  s->active_ports = -1;
  s->on_accept_cb = [](void* /* arg */, grpc_endpoint* /* ep */,
                       grpc_pollset* /* accepting_pollset */,
                       grpc_tcp_server_acceptor* /* acceptor */) {
    grpc_core::Crash("iomgr on_accept_cb callback should be unused");
  };
  s->on_accept_cb_arg = nullptr;
  s->head = nullptr;
  s->tail = nullptr;
  s->shutdown_starting.head = nullptr;
  s->shutdown_starting.tail = nullptr;
  s->shutdown_complete = grpc_core::NewClosure([](absl::Status) {
    grpc_core::Crash("iomgr shutdown_complete callback should be unused");
  });
  *server = s;
  return absl::OkStatus();
}

static void event_engine_start(grpc_tcp_server* s,
                               const std::vector<grpc_pollset*>* /*pollsets*/) {
  CHECK(s->ee_listener->Start().ok());
}

static grpc_error_handle event_engine_add_port(
    grpc_tcp_server* s, const grpc_resolved_address* addr, int* port) {
  CHECK_NE(addr, nullptr);
  CHECK_NE(port, nullptr);
  auto ee_addr = CreateResolvedAddress(*addr);
  auto out_port = s->ee_listener->Bind(ee_addr);
  *port = out_port.ok() ? *out_port : -1;
  return out_port.status();
}

static grpc_core::TcpServerFdHandler* event_engine_create_fd_handler(
    grpc_tcp_server* /* s */) {
  return nullptr;
}

static unsigned event_engine_port_fd_count(grpc_tcp_server* /* s */,
                                           unsigned /* port_index */) {
  return 0;
}

static int event_engine_port_fd(grpc_tcp_server* /* s */,
                                unsigned /* port_index */,
                                unsigned /* fd_index */) {
  return -1;
}

static grpc_tcp_server* event_engine_ref(grpc_tcp_server* s) {
  gpr_ref_non_zero(&s->refs);
  return s;
}

static void event_engine_shutdown_listeners(grpc_tcp_server* s) {
  s->ee_listener->ShutdownListeners();
}

static void event_engine_unref(grpc_tcp_server* s) {
  if (gpr_unref(&s->refs)) {
    event_engine_shutdown_listeners(s);
    gpr_mu_lock(&s->mu);
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &s->shutdown_starting);
    gpr_mu_unlock(&s->mu);
    gpr_mu_destroy(&s->mu);
    delete s->ee_listener;
    gpr_free(s);
  }
}

static void event_engine_shutdown_starting_add(
    grpc_tcp_server* s, grpc_closure* shutdown_starting) {
  gpr_mu_lock(&s->mu);
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting,
                           absl::OkStatus());
  gpr_mu_unlock(&s->mu);
}

}  // namespace

grpc_tcp_server_vtable grpc_windows_event_engine_tcp_server_vtable = {
    event_engine_create,        event_engine_start,
    event_engine_add_port,      event_engine_create_fd_handler,
    event_engine_port_fd_count, event_engine_port_fd,
    event_engine_ref,           event_engine_shutdown_starting_add,
    event_engine_unref,         event_engine_shutdown_listeners,
    tcp_pre_allocated_fd,       tcp_set_pre_allocated_fd};

#endif  // GRPC_WINSOCK_SOCKET
