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
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

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
  std::unique_ptr<EventEngine::Listener> ee_listener;
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
  // Convert UTF-8 path to Unicode.
  std::wstring wide_path;
  int needed = MultiByteToWideChar(CP_UTF8, 0, un->sun_path, -1, NULL, 0);
  if (needed <= 0) {
    return;
  }
  wide_path.resize(needed, L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, un->sun_path, -1, wide_path.data(),
                          needed) == 0) {
    // Failed to convert UTF-8 path to wide char.
    return;
  }
  // For windows we need to remove the file instead of unlink.
  DWORD attr = ::GetFileAttributesW(wide_path.data());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return;
  }
  if (attr & FILE_ATTRIBUTE_DIRECTORY || attr & FILE_ATTRIBUTE_READONLY) {
    return;
  }
  ::DeleteFileW(wide_path.data());
#else
  (void)resolved_addr;
#endif
}

static int tcp_pre_allocated_fd(grpc_tcp_server* /* s */) { return -1; }

static void tcp_set_pre_allocated_fd(grpc_tcp_server* /* s */, int /* fd */) {}

// ---- EventEngine shim ------------------------------------------------------

namespace {

static grpc_error_handle event_engine_create(grpc_closure* shutdown_complete,
                                             const EndpointConfig& config,
                                             grpc_tcp_server_cb on_accept_cb,
                                             void* on_accept_cb_arg,
                                             grpc_tcp_server** server) {
  grpc_tcp_server* s = (grpc_tcp_server*)gpr_malloc(sizeof(grpc_tcp_server));
  new (&s->ee_listener) std::unique_ptr<EventEngine::Listener>(nullptr);
  GRPC_CHECK_NE(on_accept_cb, nullptr);
  auto accept_cb = [s, on_accept_cb, on_accept_cb_arg](
                       std::unique_ptr<EventEngine::Endpoint> endpoint,
                       MemoryAllocator memory_allocator) {
    grpc_core::ExecCtx exec_ctx;
    grpc_tcp_server_acceptor* acceptor =
        static_cast<grpc_tcp_server_acceptor*>(gpr_malloc(sizeof(*acceptor)));
    acceptor->from_server = s;
    acceptor->port_index = -1;
    acceptor->fd_index = -1;
    acceptor->external_connection = false;
    acceptor->pending_data = nullptr;
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
    GRPC_CHECK_NE(tmp_quota, nullptr);
    resource_quota =
        reinterpret_cast<grpc_core::ResourceQuota*>(tmp_quota)->Ref();
  }
  gpr_ref_init(&s->refs, 1);
  gpr_mu_init(&s->mu);
  EventEngine* engine = reinterpret_cast<EventEngine*>(
      config.GetVoidPointer(GRPC_INTERNAL_ARG_EVENT_ENGINE));
  GRPC_CHECK_NE(engine, nullptr);
  auto listener = engine->CreateListener(
      std::move(accept_cb), std::move(on_shutdown), config,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          resource_quota->memory_quota()));
  GRPC_RETURN_IF_ERROR(listener.status());
  GRPC_CHECK_NE(listener->get(), nullptr);
  GRPC_TRACE_LOG(event_engine, INFO)
      << "EventEngine::" << engine << ": created Listener::" << listener->get();
  s->ee_listener = std::move(*listener);
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
  GRPC_CHECK(s->ee_listener->Start().ok());
}

static grpc_error_handle event_engine_add_port(
    grpc_tcp_server* s, const grpc_resolved_address* addr, int* port) {
  GRPC_CHECK_NE(addr, nullptr);
  GRPC_CHECK_NE(port, nullptr);
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
  auto* iomgr_compatible_listener =
      grpc_event_engine::experimental::QueryExtension<
          grpc_event_engine::experimental::IomgrCompatibleListener>(
          s->ee_listener.get());
  if (iomgr_compatible_listener != nullptr) {
    iomgr_compatible_listener->Shutdown();
  }
}

static void event_engine_unref(grpc_tcp_server* s) {
  if (gpr_unref(&s->refs)) {
    event_engine_shutdown_listeners(s);
    gpr_mu_lock(&s->mu);
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &s->shutdown_starting);
    gpr_mu_unlock(&s->mu);
    gpr_mu_destroy(&s->mu);
    std::destroy_at(&s->ee_listener);
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
