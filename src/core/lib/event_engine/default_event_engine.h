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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_DEFAULT_EVENT_ENGINE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_DEFAULT_EVENT_ENGINE_H

#include <grpc/support/port_platform.h>

#include <memory>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/promise/context.h"

namespace grpc_core {
template <>
struct ContextType<grpc_event_engine::experimental::EventEngine> {};
}  // namespace grpc_core

namespace grpc_event_engine {
namespace experimental {

/// Access the shared global EventEngine instance.
///
/// The concept of a global EventEngine may go away in a post-iomgr world.
/// Strongly consider whether you could use \a CreateEventEngine instead.
std::shared_ptr<EventEngine> GetDefaultEventEngine();

/// On ingress, ensure that an EventEngine exists in channel args via
/// preconditioning.
void RegisterEventEngineChannelArgPreconditioning(
    grpc_core::CoreConfiguration::Builder* builder);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_DEFAULT_EVENT_ENGINE_H
