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
#include <vector>

#include <grpc/support/cpu.h>

#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/no_destruct.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

grpc_core::NoDestruct<std::vector<std::weak_ptr<ForkableInterface>>>
    g_forkable_instances;

bool g_registered{false};

#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

void grpc_prefork() {
  gpr_log(GPR_INFO, "grpc_prefork()");
  for (auto& instance : *g_forkable_instances) {
    auto shared = instance.lock();
    if (shared) {
      shared->PrepareFork();
    }
  }
}

void grpc_postfork_parent() {
  gpr_log(GPR_INFO, "grpc_postfork_parent()");
  for (auto& instance : *g_forkable_instances) {
    auto shared = instance.lock();
    if (shared) {
      shared->PostforkParent();
    }
  }
}

void grpc_postfork_child() {
  gpr_log(GPR_INFO, "grpc_postfork_child()");
  for (auto& instance : *g_forkable_instances) {
    auto shared = instance.lock();
    if (shared) {
      shared->PostforkChild();
    }
  }
}

#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

}  // namespace

std::shared_ptr<ThreadPool> MakeThreadPool(size_t /* reserve_threads */) {
  auto shared = std::make_shared<WorkStealingThreadPool>(
      grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 16u));
  if (!std::exchange(g_registered, true)) {
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
    pthread_atfork(grpc_prefork, grpc_postfork_parent, grpc_postfork_child);
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  }
  g_forkable_instances->emplace_back(shared);
  return shared;
}

}  // namespace experimental
}  // namespace grpc_event_engine
