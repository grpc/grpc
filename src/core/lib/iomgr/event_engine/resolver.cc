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

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/work_serializer.h"

namespace {
using ::grpc_event_engine::experimental::EventEngine;

EventEngine::DNSResolver::LookupHostnameCallback
GrpcClosureToLookupHostnameCallback(grpc_closure* closure) {
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

void resolve_address(const char* addr, const char* default_port,
                     grpc_pollset_set* interested_parties,
                     grpc_closure* on_done,
                     grpc_resolved_addresses** addresses) {
  (void)addr;
  (void)default_port;
  (void)interested_parties;
  (void)on_done;
  (void)addresses;
}

grpc_error* blocking_resolve_address(const char* name, const char* default_port,
                                     grpc_resolved_addresses** addresses) {
  (void)name;
  (void)default_port;
  (void)addresses;
  return GRPC_ERROR_NONE;
}

}  // namespace

grpc_address_resolver_vtable grpc_event_engine_resolver_vtable{
    resolve_address, blocking_resolve_address};

#endif  // GRPC_EVENT_ENGINE_TEST
