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
//
/// grpc_closure to std::function conversions for an EventEngine-based iomgr
#ifndef GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_UTIL_H
#define GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_UTIL_H
#ifdef GRPC_EVENT_ENGINE_TEST

#include <grpc/support/port_platform.h>

#include <functional>

#include "absl/status/status.h"
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/event_engine/endpoint.h"

namespace grpc_event_engine {
namespace experimental {

EventEngine::Callback event_engine_closure_to_callback(grpc_closure* closure);

EventEngine::OnConnectCallback event_engine_closure_to_on_connect_callback(
    grpc_closure* closure, grpc_event_engine_endpoint* grpc_endpoint_out);

EventEngine::Listener::AcceptCallback event_engine_closure_to_accept_callback(
    grpc_closure* closure);

EventEngine::DNSResolver::LookupHostnameCallback
event_engine_closure_to_lookup_hostname_callback(grpc_closure* closure);

EventEngine::DNSResolver::LookupSRVCallback
event_engine_closure_to_lookup_srv_callback(grpc_closure* closure);

EventEngine::DNSResolver::LookupTXTCallback
event_engine_closure_to_lookup_txt_callback(grpc_closure* closure);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif
#endif  // GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_UTIL_H
