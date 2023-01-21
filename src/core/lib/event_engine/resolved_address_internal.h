// Copyright 2022 gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_RESOLVED_ADDRESS_INTERNAL_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_RESOLVED_ADDRESS_INTERNAL_H
#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_event_engine {
namespace experimental {

EventEngine::ResolvedAddress CreateResolvedAddress(
    const grpc_resolved_address& addr);

grpc_resolved_address CreateGRPCResolvedAddress(
    const EventEngine::ResolvedAddress& ra);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_RESOLVED_ADDRESS_INTERNAL_H
