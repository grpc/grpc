// Copyright 2023 The gRPC Authors
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

#include <stddef.h>

#include <memory>

#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"
#include "src/core/lib/gprpp/no_destruct.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
grpc_core::NoDestruct<ObjectGroupForkHandler> g_thread_pool_fork_manager;

class ThreadPoolForkCallbackMethods {
 public:
  static void Prefork() { g_thread_pool_fork_manager->Prefork(); }
  static void PostforkParent() { g_thread_pool_fork_manager->PostforkParent(); }
  static void PostforkChild() { g_thread_pool_fork_manager->PostforkChild(); }
};
}  // namespace

std::shared_ptr<ThreadPool> MakeThreadPool(size_t reserve_threads) {
  auto thread_pool = std::make_shared<WorkStealingThreadPool>(reserve_threads);
  g_thread_pool_fork_manager->RegisterForkable(
      thread_pool, ThreadPoolForkCallbackMethods::Prefork,
      ThreadPoolForkCallbackMethods::PostforkParent,
      ThreadPoolForkCallbackMethods::PostforkChild);
  return thread_pool;
}

}  // namespace experimental
}  // namespace grpc_event_engine
