// Copyright 2021 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#ifdef GRPC_USE_EVENT_ENGINE
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/event_engine_factory.h"
#include "src/core/lib/event_engine/sockaddr.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/event_engine/closure.h"
#include "src/core/lib/iomgr/event_engine/endpoint.h"
#include "src/core/lib/iomgr/event_engine/pollset.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/surface/init.h"
#include "src/core/lib/transport/error_utils.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace {
using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::GrpcClosureToStatusCallback;
using ::grpc_event_engine::experimental::SliceAllocator;
using ::grpc_event_engine::experimental::SliceAllocatorFactory;
using ::grpc_event_engine::experimental::SliceBuffer;
}  // namespace

class WrappedInternalSliceAllocator : public SliceAllocator {
 public:
  explicit WrappedInternalSliceAllocator(grpc_slice_allocator* slice_allocator)
      : slice_allocator_(slice_allocator) {}

  ~WrappedInternalSliceAllocator() {
    grpc_slice_allocator_destroy(slice_allocator_);
  }

  absl::Status Allocate(size_t size, SliceBuffer* dest,
                        SliceAllocator::AllocateCallback cb) override {
    // TODO(nnoble): requires the SliceBuffer definition.
    grpc_slice_allocator_allocate(
        slice_allocator_, size, 1, grpc_slice_allocator_intent::kReadBuffer,
        dest->RawSliceBuffer(),
        [](void* arg, grpc_error_handle error) {
          auto cb = static_cast<SliceAllocator::AllocateCallback*>(arg);
          (*cb)(grpc_error_to_absl_status(error));
          delete cb;
        },
        new SliceAllocator::AllocateCallback(cb));
    return absl::OkStatus();
  }

 private:
  grpc_slice_allocator* slice_allocator_;
};

class WrappedInternalSliceAllocatorFactory : public SliceAllocatorFactory {
 public:
  explicit WrappedInternalSliceAllocatorFactory(
      grpc_slice_allocator_factory* slice_allocator_factory)
      : slice_allocator_factory_(slice_allocator_factory) {}

  ~WrappedInternalSliceAllocatorFactory() {
    grpc_slice_allocator_factory_destroy(slice_allocator_factory_);
  }

  std::unique_ptr<SliceAllocator> CreateSliceAllocator(
      absl::string_view peer_name) override {
    return absl::make_unique<WrappedInternalSliceAllocator>(
        grpc_slice_allocator_factory_create_slice_allocator(
            slice_allocator_factory_, peer_name));
  };

 private:
  grpc_slice_allocator_factory* slice_allocator_factory_;
};

struct grpc_tcp_server {
  explicit grpc_tcp_server(std::unique_ptr<EventEngine::Listener> listener)
      : refcount(1, GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace) ? "tcp" : nullptr),
        listener(std::move(listener)) {
    shutdown_starting.head = nullptr;
    shutdown_starting.tail = nullptr;
  };
  ~grpc_tcp_server() {
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &shutdown_starting);
    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_core::RefCount refcount;
  grpc_core::Mutex mu;
  std::unique_ptr<EventEngine::Listener> listener;
  grpc_closure_list shutdown_starting ABSL_GUARDED_BY(mu);
  grpc_tcp_server_cb on_accept_internal;
  void* on_accept_internal_arg;
};

namespace {

/// Converts a grpc_closure to an EventEngine Callback. The closure is expected
/// to already be initialized.
EventEngine::OnConnectCallback GrpcClosureToOnConnectCallback(
    grpc_closure* closure, grpc_endpoint** endpoint_ptr) {
  return [closure, endpoint_ptr](
             absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
    grpc_core::ExecCtx exec_ctx;
    if (endpoint.ok()) {
      auto* grpc_endpoint_out =
          reinterpret_cast<grpc_event_engine_endpoint*>(*endpoint_ptr);
      grpc_endpoint_out->endpoint = std::move(*endpoint);
    } else {
      grpc_endpoint_destroy(*endpoint_ptr);
      *endpoint_ptr = nullptr;
    }
    grpc_core::Closure::Run(DEBUG_LOCATION, closure,
                            absl_status_to_grpc_error(endpoint.status()));
    exec_ctx.Flush();
    grpc_pollset_ee_broadcast_event();
  };
}

/// Usage note: this method does not take ownership of any pointer arguments.
void tcp_connect(grpc_closure* on_connect, grpc_endpoint** endpoint,
                 grpc_slice_allocator* slice_allocator,
                 grpc_pollset_set* /* interested_parties */,
                 const grpc_channel_args* channel_args,
                 const grpc_resolved_address* addr, grpc_millis deadline) {
  grpc_event_engine_endpoint* ee_endpoint =
      reinterpret_cast<grpc_event_engine_endpoint*>(
          grpc_tcp_create(channel_args, grpc_sockaddr_to_uri(addr)));
  *endpoint = &ee_endpoint->base;
  EventEngine::OnConnectCallback ee_on_connect =
      GrpcClosureToOnConnectCallback(on_connect, endpoint);
  auto ee_slice_allocator =
      absl::make_unique<WrappedInternalSliceAllocator>(slice_allocator);
  EventEngine::ResolvedAddress ra(reinterpret_cast<const sockaddr*>(addr->addr),
                                  addr->len);
  absl::Time ee_deadline = grpc_core::ToAbslTime(
      grpc_millis_to_timespec(deadline, GPR_CLOCK_MONOTONIC));
  ChannelArgsEndpointConfig endpoint_config(channel_args);
  absl::Status connected = GetDefaultEventEngine()->Connect(
      ee_on_connect, ra, endpoint_config, std::move(ee_slice_allocator),
      ee_deadline);
  if (!connected.ok()) {
    // EventEngine failed to start an asynchronous connect.
    grpc_endpoint_destroy(*endpoint);
    *endpoint = nullptr;
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_connect,
                            absl_status_to_grpc_error(connected));
  }
}

grpc_error_handle tcp_server_create(
    grpc_closure* shutdown_complete, const grpc_channel_args* args,
    grpc_slice_allocator_factory* slice_allocator_factory,
    grpc_tcp_server** server) {
  ChannelArgsEndpointConfig endpoint_config(args);
  auto ee_slice_allocator_factory =
      absl::make_unique<WrappedInternalSliceAllocatorFactory>(
          slice_allocator_factory);
  EventEngine* event_engine = GetDefaultEventEngine();
  absl::StatusOr<std::unique_ptr<EventEngine::Listener>> listener =
      event_engine->CreateListener(
          [server](std::unique_ptr<EventEngine::Endpoint> ee_endpoint,
                   const SliceAllocator& /*slice_allocator*/) {
            grpc_core::ExecCtx exec_ctx;
            GPR_ASSERT((*server)->on_accept_internal != nullptr);
            grpc_event_engine_endpoint* iomgr_endpoint =
                grpc_tcp_server_endpoint_create(std::move(ee_endpoint));
            grpc_tcp_server_acceptor* acceptor =
                static_cast<grpc_tcp_server_acceptor*>(
                    gpr_zalloc(sizeof(*acceptor)));
            acceptor->from_server = *server;
            acceptor->external_connection = false;
            (*server)->on_accept_internal((*server)->on_accept_internal_arg,
                                          &iomgr_endpoint->base, nullptr,
                                          acceptor);
            exec_ctx.Flush();
            grpc_pollset_ee_broadcast_event();
          },
          GrpcClosureToStatusCallback(shutdown_complete), endpoint_config,
          std::move(ee_slice_allocator_factory));
  if (!listener.ok()) {
    return absl_status_to_grpc_error(listener.status());
  }
  *server = new grpc_tcp_server(std::move(*listener));
  return GRPC_ERROR_NONE;
}

void tcp_server_start(grpc_tcp_server* server,
                      const std::vector<grpc_pollset*>* /* pollsets */,
                      grpc_tcp_server_cb on_accept_cb, void* cb_arg) {
  server->on_accept_internal = on_accept_cb;
  server->on_accept_internal_arg = cb_arg;
  // The iomgr API does not handle situations where the server cannot start, so
  // a crash may be preferable for now.
  GPR_ASSERT(server->listener->Start().ok());
}

grpc_error_handle tcp_server_add_port(grpc_tcp_server* s,
                                      const grpc_resolved_address* addr,
                                      int* out_port) {
  EventEngine::ResolvedAddress ra(reinterpret_cast<const sockaddr*>(addr->addr),
                                  addr->len);
  auto port = s->listener->Bind(ra);
  if (!port.ok()) {
    return absl_status_to_grpc_error(port.status());
  }
  *out_port = *port;
  return GRPC_ERROR_NONE;
}

grpc_core::TcpServerFdHandler* tcp_server_create_fd_handler(
    grpc_tcp_server* /* s */) {
  // EventEngine-iomgr does not support fds.
  return nullptr;
}

unsigned tcp_server_port_fd_count(grpc_tcp_server* /* s */,
                                  unsigned /* port_index */) {
  return 0;
}

int tcp_server_port_fd(grpc_tcp_server* /* s */, unsigned /* port_index */,
                       unsigned /* fd_index */) {
  // Note: only used internally
  return -1;
}

grpc_tcp_server* tcp_server_ref(grpc_tcp_server* s) {
  s->refcount.Ref(DEBUG_LOCATION, "server ref");
  return s;
}

void tcp_server_shutdown_starting_add(grpc_tcp_server* s,
                                      grpc_closure* shutdown_starting) {
  grpc_core::MutexLock lock(&s->mu);
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting,
                           GRPC_ERROR_NONE);
}

void tcp_server_unref(grpc_tcp_server* s) {
  if (GPR_UNLIKELY(s->refcount.Unref(DEBUG_LOCATION, "server unref"))) {
    delete s;
  }
}

// No-op, all are handled on listener unref
void tcp_server_shutdown_listeners(grpc_tcp_server* /* s */) {}

}  // namespace

grpc_tcp_client_vtable grpc_event_engine_tcp_client_vtable = {tcp_connect};
grpc_tcp_server_vtable grpc_event_engine_tcp_server_vtable = {
    tcp_server_create,        tcp_server_start,
    tcp_server_add_port,      tcp_server_create_fd_handler,
    tcp_server_port_fd_count, tcp_server_port_fd,
    tcp_server_ref,           tcp_server_shutdown_starting_add,
    tcp_server_unref,         tcp_server_shutdown_listeners};

// Methods that are expected to exist elsewhere in the codebase.

struct grpc_fd {
  int fd;
};

grpc_fd* grpc_fd_create(int /* fd */, const char* /* name */,
                        bool /* track_err */) {
  return nullptr;
}

grpc_endpoint* grpc_tcp_client_create_from_fd(
    grpc_fd* /* fd */, const grpc_channel_args* /* channel_args */,
    absl::string_view /* addr_str */,
    grpc_slice_allocator* slice_allocator /* slice_allocator */) {
  grpc_slice_allocator_destroy(slice_allocator);
  return nullptr;
}

#endif  // GRPC_USE_EVENT_ENGINE
