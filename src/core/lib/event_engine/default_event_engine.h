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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_DEFAULT_EVENT_ENGINE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_DEFAULT_EVENT_ENGINE_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/util/debug_location.h"

namespace grpc_event_engine {
namespace experimental {

/// Access the shared global EventEngine instance.
///
/// GetDefaultEventEngine is a lazy thing: either a shared global EventEngine
/// instance exists and will be returned, or that shared global instance will be
/// created and returned. The returned shared_ptr<EventEngine>'s life is
/// determined by the shared_ptr, and therefore EventEngines may be created and
/// destroyed multiple times through the life of your gRPC process, there is no
/// guarantee of one persistent global instance like in iomgr.
///
/// Why would we do this? To begin with, users may provide their own EventEngine
/// instances on channel or server creation; if they do that, there is some
/// chance that a default EventEngine instance will not have to be created, and
/// applications will not have to pay the (probably small) price of
/// instantiating an engine they do not own. The other major consideration is
/// that gRPC shutdown is likely going away. Without a well-defined point at
/// which a persistent global EventEngine instance can safely be shut down, we
/// risk undefined behavior and various documented breakages if the engine is
/// not shut down cleanly before the process exits. Therefore, allowing the
/// EventEngine lifetimes to be determined by the scopes in which they are
/// needed is a fine solution.
///
/// If you're writing code that needs an EventEngine, prefer 1) getting the
/// EventEngine from somewhere it is already cached - preconditioned
/// ChannelArgs, or the channel stack for example - or 2) if not otherwise
/// available, call \a GetDefaultEventEngine and save that shared_ptr to ensure
/// the Engine's lifetime is at least as long as you need it to be.
std::shared_ptr<EventEngine> GetDefaultEventEngine(
    grpc_core::SourceLocation location = grpc_core::SourceLocation());

/// On ingress, ensure that an EventEngine exists in channel args via
/// preconditioning.
void RegisterEventEngineChannelArgPreconditioning(
    grpc_core::CoreConfiguration::Builder* builder);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_DEFAULT_EVENT_ENGINE_H
