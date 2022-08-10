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

#ifndef GRPC_INTERNAL_CPP_DYNAMIC_THREAD_POOL_H
#define GRPC_INTERNAL_CPP_DYNAMIC_THREAD_POOL_H

#include <functional>
#include <list>
#include <queue>

#include "src/core/lib/event_engine/thread_pool.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/cpp/server/thread_pool_interface.h"

namespace grpc {

class DynamicThreadPool final : public ThreadPoolInterface {
 public:
  explicit DynamicThreadPool(int reserve_threads)
      : thread_pool_(reserve_threads) {}

  void Add(const std::function<void()>& callback) override {
    thread_pool_.Add(callback);
  }

 private:
  grpc_event_engine::experimental::ThreadPool thread_pool_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_DYNAMIC_THREAD_POOL_H
