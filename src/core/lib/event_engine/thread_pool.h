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

#include <queue>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"

namespace grpc_event_engine {
namespace experimental {

class ThreadPool final : public grpc_event_engine::experimental::Forkable {
 public:
  explicit ThreadPool(int reserve_threads);
  ~ThreadPool() override;

  void Add(absl::AnyInvocable<void()> callback);

  // Forkable
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

 private:
  class Thread {
   public:
    explicit Thread(ThreadPool* pool);
    ~Thread();

   private:
    ThreadPool* pool_;
    grpc_core::Thread thd_;
    void ThreadFunc();
  };

  void ThreadFunc();
  void StartNThreadsLocked(int n) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);
  static void ReapThreads(std::vector<Thread*>* tlist);

  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  grpc_core::CondVar shutdown_cv_;
  grpc_core::CondVar fork_cv_;
  bool shutdown_;
  std::queue<absl::AnyInvocable<void()>> callbacks_;
  int reserve_threads_;
  int nthreads_;
  int threads_waiting_;
  std::vector<Thread*> dead_threads_;
  bool forking_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_THREAD_POOL_H
