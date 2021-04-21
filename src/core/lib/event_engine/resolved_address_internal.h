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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_RESOLVED_ADDRESS_INTERNAL_H
#define GRPC_CORE_LIB_EVENT_ENGINE_RESOLVED_ADDRESS_INTERNAL_H
#ifdef GRPC_EVENT_ENGINE_TEST

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/resolve_address.h"

namespace grpc_event_engine {
namespace experimental {

std::string ResolvedAddressToURI(const EventEngine::ResolvedAddress* addr);

EventEngine::ResolvedAddress CreateResolvedAddress(
    const grpc_resolved_address* addr);

grpc_resolved_address CreateGRPCResolvedAddress(
    const EventEngine::ResolvedAddress* ra);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif
#endif  // GRPC_CORE_LIB_EVENT_ENGINE_RESOLVED_ADDRESS_INTERNAL_H
