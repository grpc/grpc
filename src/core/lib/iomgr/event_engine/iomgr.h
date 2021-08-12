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
#ifndef GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_IOMGR_H
#define GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_IOMGR_H
#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>

/// This can be called anywhere in the EventEngine-based iomgr impl where we
/// need to access the global EventEngine instance.
grpc_event_engine::experimental::EventEngine* grpc_iomgr_event_engine();

namespace grpc_core {

/// Set the default \a EventEngine.
///
/// The iomgr interfaces conceptually expose a global singleton iomgr instance
/// that is shared throughout gRPC. To accomodate an EventEngine-based iomgr
/// implementation, this method sets the default EventEngine that will be used.
/// The default EventEngine can only be set once during the lifetime of gRPC.
/// This method must be called before \a grpc_init() (truly, before
/// \a grpc_iomgr_init()). This engine is shut down along with iomgr.
///
/// This is an internal method, not intended for public use. Public APIs are
/// being planned.
void SetDefaultEventEngine(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine> event_engine);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_IOMGR_H
