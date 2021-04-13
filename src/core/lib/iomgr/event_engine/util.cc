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
#if defined(GRPC_EVENT_ENGINE_TEST)

#include <grpc/support/port_platform.h>

#include <functional>

#include "absl/status/status.h"
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/event_engine/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_event_engine {
namespace experimental {

std::shared_ptr<EventEngine> GetDefaultEventEngine() {
  // TODO(nnoble): instantiate a singleton LibuvEventEngine
  return nullptr;
}

// grpc_closure to std::function conversions for an EventEngine-based iomgr
EventEngine::Callback event_engine_closure_to_callback(grpc_closure* closure) {
  return [&](absl::Status status) {
    // TODO(hork): Do we need to add grpc_error to closure's error data?
    // if (!status.ok()) {
    //   closure->error_data.error = grpc_error_add_child(
    //       closure->error_data.error,
    //       GRPC_ERROR_CREATE_FROM_COPIED_STRING(status.ToString().c_str()));
    // }
    grpc_core::Closure::Run(DEBUG_LOCATION, closure,
                            absl_status_to_grpc_error(status));
  };
}

// DO NOT SUBMIT NOTES: the closure is already initialized, and does not take an
// Endpoint. See chttp2_connector:L74. Instead, the closure arg contains a ptr
// to the endpoint that iomgr is expected to populate. When gRPC eventually uses
// the EventEngine directly, closures will be replaced with EE callback types.
// For now, the Endpiont can be ignored as the closure is expected to have
// access to it already.
EventEngine::OnConnectCallback event_engine_closure_to_on_connect_callback(
    grpc_closure* closure, grpc_event_engine_endpoint* grpc_endpoint_out) {
  return [&](absl::Status status, EventEngine::Endpoint* endpoint) {
    grpc_endpoint_out->endpoint = endpoint;
    // TODO(hork): Do we need to add grpc_error to closure's error data?
    grpc_core::Closure::Run(DEBUG_LOCATION, closure,
                            absl_status_to_grpc_error(status));
  };
}

EventEngine::Listener::AcceptCallback event_engine_closure_to_accept_callback(
    grpc_closure* closure, void* arg) {
  (void)closure;
  (void)arg;
  return [](absl::Status, EventEngine::Endpoint*) {};
}

EventEngine::DNSResolver::LookupHostnameCallback
event_engine_closure_to_lookup_hostname_callback(grpc_closure* closure) {
  (void)closure;
  return [](absl::Status, std::vector<EventEngine::ResolvedAddress>) {};
}

EventEngine::DNSResolver::LookupSRVCallback
event_engine_closure_to_lookup_srv_callback(grpc_closure* closure) {
  (void)closure;
  return [](absl::Status, std::vector<EventEngine::DNSResolver::SRVRecord>) {};
}

EventEngine::DNSResolver::LookupTXTCallback
event_engine_closure_to_lookup_txt_callback(grpc_closure* closure) {
  (void)closure;
  return [](absl::Status, std::string) {};
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_TEST
