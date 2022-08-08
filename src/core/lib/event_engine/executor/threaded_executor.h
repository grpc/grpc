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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_EXECUTOR_THREADED_EXECUTOR_H
#define GRPC_CORE_LIB_EVENT_ENGINE_EXECUTOR_THREADED_EXECUTOR_H

#include <grpc/support/port_platform.h>

#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/executor/executor.h"
#include "src/core/lib/event_engine/thread_pool.h"

namespace grpc_event_engine {
namespace experimental {

class ThreadedExecutor : public Executor {
 public:
  explicit ThreadedExecutor(int reserve_threads);
  ~ThreadedExecutor() override = default;
  void Run(EventEngine::Closure* closure) override;
  void Run(absl::AnyInvocable<void()> closure) override;

 private:
  ThreadPool thread_pool_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_EXECUTOR_THREADED_EXECUTOR_H