// Copyright 2022 gRPC authors.
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

#include "src/core/lib/event_engine/iomgr_engine/windows/windows_create_listener.h"

namespace grpc_event_engine {
namespace experimental {

#ifndef GPR_WINDOWS

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
IomgrEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    std::function<void(absl::Status)> on_shutdown, const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  GPR_ASSERT(false &&
             "Build misconfiguration: this should only compile on Windows");
}

#else  // GPR_WINDOWS

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
IomgrEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    std::function<void(absl::Status)> on_shutdown, const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  GPR_ASSERT(false && "unimplemented");
}

#endif  // GPR_WINDOWS
}  // namespace experimental
}  // namespace grpc_event_engine
