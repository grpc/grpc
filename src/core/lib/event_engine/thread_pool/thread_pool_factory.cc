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

#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"

namespace grpc_event_engine::experimental {

std::shared_ptr<ThreadPool> MakeThreadPool(size_t reserve_threads) {
  auto thread_pool = std::make_shared<WorkStealingThreadPool>(reserve_threads);
  return thread_pool;
}

}  // namespace grpc_event_engine::experimental
