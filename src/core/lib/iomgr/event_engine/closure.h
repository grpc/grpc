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
#ifndef GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_CLOSURE_H
#define GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_CLOSURE_H

#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_event_engine {
namespace experimental {

/// Creates a callback that takes an error status argument.
std::function<void(absl::Status)> GrpcClosureToStatusCallback(
    grpc_closure* closure);

/// Create a callback that *does not* take an error status argument.
std::function<void()> GrpcClosureToCallback(grpc_closure* closure);

/// Creates a callback that *does not* take an error status argument.
/// This version has a pre-bound error.
std::function<void()> GrpcClosureToCallback(grpc_closure* closure,
                                            grpc_error_handle error);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_CLOSURE_H
