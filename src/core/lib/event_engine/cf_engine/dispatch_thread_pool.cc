//
//
// Copyright 2025 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include <dispatch/dispatch.h>

#include "src/core/lib/event_engine/cf_engine/dispatch_thread_pool.h"
#include "src/core/lib/event_engine/common_closures.h"

namespace grpc_event_engine::experimental {

void DispatchThreadPool::Run(absl::AnyInvocable<void()> callback) {
  Run(SelfDeletingClosure::Create(std::move(callback)));
}

void DispatchThreadPool::Run(EventEngine::Closure* closure) {
  dispatch_async_f(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), closure,
                   [](void* context) {
                     auto* closure =
                         static_cast<EventEngine::Closure*>(context);
                     closure->Run();
                   });
}

}  // namespace grpc_event_engine::experimental

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE
