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

#include "src/core/lib/event_engine/executor/threaded_executor.h"

#include <utility>

#include "src/core/lib/event_engine/forkable.h"

namespace grpc_event_engine {
namespace experimental {

ThreadedExecutor::ThreadedExecutor(int reserve_threads)
    : thread_pool_(reserve_threads) {
  ManageForkable(&thread_pool_);
};

ThreadedExecutor::~ThreadedExecutor() { StopManagingForkable(&thread_pool_); }

void ThreadedExecutor::Run(EventEngine::Closure* closure) {
  thread_pool_.Add([closure]() { closure->Run(); });
}

void ThreadedExecutor::Run(absl::AnyInvocable<void()> closure) {
  thread_pool_.Add(std::move(closure));
}

}  // namespace experimental
}  // namespace grpc_event_engine
