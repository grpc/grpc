// Copyright 2021 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/server/binder_server.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc {
namespace experimental {
namespace binder {

void* GetEndpointBinder(const std::string& service) {
  return grpc_get_endpoint_binder(service);
}

void AddEndpointBinder(const std::string& service, void* endpoint_binder) {
  grpc_add_endpoint_binder(service, endpoint_binder);
}

void RemoveEndpointBinder(const std::string& service) {
  grpc_remove_endpoint_binder(service);
}

}  // namespace binder
}  // namespace experimental
}  // namespace grpc

grpc_core::Mutex* g_endpoint_binder_pool_mu = nullptr;
absl::flat_hash_map<std::string, void*>* g_endpoint_binder_pool = nullptr;

void grpc_endpoint_binder_pool_init() {
  g_endpoint_binder_pool_mu = new grpc_core::Mutex();
  g_endpoint_binder_pool = new absl::flat_hash_map<std::string, void*>();
}

void grpc_endpoint_binder_pool_shutdown() {
  g_endpoint_binder_pool_mu->Lock();
  delete g_endpoint_binder_pool;
  g_endpoint_binder_pool_mu->Unlock();
  delete g_endpoint_binder_pool_mu;
}

void grpc_add_endpoint_binder(const std::string& service,
                              void* endpoint_binder) {
  grpc_core::MutexLock lock(g_endpoint_binder_pool_mu);
  (*g_endpoint_binder_pool)[service] = endpoint_binder;
}

void grpc_remove_endpoint_binder(const std::string& service) {
  grpc_core::MutexLock lock(g_endpoint_binder_pool_mu);
  g_endpoint_binder_pool->erase(service);
}

void* grpc_get_endpoint_binder(const std::string& service) {
  grpc_core::MutexLock lock(g_endpoint_binder_pool_mu);
  auto iter = g_endpoint_binder_pool->find(service);
  return iter == g_endpoint_binder_pool->end() ? nullptr : iter->second;
}
