//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "src/cpp/server/dynamic_thread_pool.h"
#include "src/cpp/server/thread_pool_interface.h"

#ifndef GRPC_CUSTOM_DEFAULT_THREAD_POOL

namespace grpc {
namespace {

ThreadPoolInterface* CreateDefaultThreadPoolImpl() {
  return new DynamicThreadPool();
}

CreateThreadPoolFunc g_ctp_impl = CreateDefaultThreadPoolImpl;

}  // namespace

ThreadPoolInterface* CreateDefaultThreadPool() { return g_ctp_impl(); }

void SetCreateThreadPool(CreateThreadPoolFunc func) { g_ctp_impl = func; }

}  // namespace grpc

#endif  // !GRPC_CUSTOM_DEFAULT_THREAD_POOL
