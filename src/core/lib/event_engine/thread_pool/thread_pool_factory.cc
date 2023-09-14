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

#include <grpc/support/cpu.h>

#include "src/core/lib/event_engine/thread_pool/original_thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/useful.h"

namespace grpc_event_engine {
namespace experimental {

std::shared_ptr<ThreadPool> MakeThreadPool(size_t reserve_threads) {
// TODO(hork): remove when the listener flake is identified
#ifdef GPR_WINDOWS
  if (grpc_core::IsEventEngineListenerEnabled()) {
    return std::make_shared<WorkStealingThreadPool>(
        grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 16u));
  }
#endif
  if (grpc_core::IsWorkStealingEnabled()) {
    return std::make_shared<WorkStealingThreadPool>(
        grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 16u));
  }
  return std::make_shared<OriginalThreadPool>(reserve_threads);
}

}  // namespace experimental
}  // namespace grpc_event_engine
