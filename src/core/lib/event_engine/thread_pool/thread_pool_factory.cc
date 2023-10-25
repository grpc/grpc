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
#include <utility>

#include <grpc/support/cpu.h>

#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/no_destruct.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
grpc_core::NoDestruct<ObjectGroupForkHandler> g_thread_pool_fork_manager;
bool g_registered = false;

void Prefork() { g_thread_pool_fork_manager->Prefork(); }
void PostforkParent() { g_thread_pool_fork_manager->PostforkParent(); }
void PostforkChild() { g_thread_pool_fork_manager->PostforkChild(); }
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

}  // namespace

std::shared_ptr<ThreadPool> MakeThreadPool(size_t /* reserve_threads */) {
  auto shared = std::make_shared<WorkStealingThreadPool>(
      grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 16u));
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  GRPC_FORK_TRACE_LOG_STRING("ThreadPool register forkable");
  g_thread_pool_fork_manager->RegisterForkable(shared);
  if (!std::exchange(g_registered, true)) {
    pthread_atfork(Prefork, PostforkParent, PostforkChild);
  }
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  return shared;
}

}  // namespace experimental
}  // namespace grpc_event_engine
