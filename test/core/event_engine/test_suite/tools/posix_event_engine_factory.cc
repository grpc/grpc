// Copyright 2023 gRPC authors.
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

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"  // IWYU pragma: keep
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include "src/core/lib/event_engine/posix_engine/posix_engine.h"

absl::AnyInvocable<
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>(void)>
CustomEventEngineFactory() {
  return []() {
    return grpc_event_engine::experimental::PosixEventEngine::
        MakePosixEventEngine();
  };
}

#else

absl::AnyInvocable<
    std::unique_ptr<grpc_event_engine::experimental::EventEngine>(void)>
CustomEventEngineFactory() {
  CHECK(false) <<  "This tool was not built for Posix environments.");
}

#endif
