/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
#define GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <memory>
#include <queue>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/cpu.h>

#include "src/core/lib/event_engine/executor/executor.h"
#include "src/core/lib/event_engine/forkable.h"

namespace grpc_event_engine {
namespace experimental {

class ThreadPoolImpl;
class ThreadPool final : public Forkable, public Executor {
 public:
  ThreadPool();
  // Asserts Quiesce was called.
  ~ThreadPool() override = default;

  void Quiesce();

  // Run must not be called after Quiesce completes
  void Run(absl::AnyInvocable<void()> callback) override;
  void Run(EventEngine::Closure* closure) override;

  // Forkable
  // Ensures that the thread pool is empty before forking.
  // TODO(hork): this is only for testing. Consider exposing the impl for
  // testing
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

 private:
  std::shared_ptr<ThreadPoolImpl> impl_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
