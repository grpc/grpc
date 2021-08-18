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

absl::flat_hash_map<std::string, void*>* EndpointBinderPool::pool_ = nullptr;

void* EndpointBinderPool::GetEndpointBinder(const std::string& service) {
  if (!pool_) return nullptr;
  auto iter = pool_->find(service);
  return iter == pool_->end() ? nullptr : iter->second;
}

void EndpointBinderPool::AddEndpointBinder(const std::string& service,
                                           void* endpoint_binder) {
  if (!pool_) {
    pool_ = new absl::flat_hash_map<std::string, void*>();
  }
  (*pool_)[service] = endpoint_binder;
}

void EndpointBinderPool::RemoveEndpointBinder(const std::string& service) {
  if (pool_) {
    pool_->erase(service);
  }
}

void EndpointBinderPool::Reset() { delete pool_; }

}  // namespace experimental
}  // namespace grpc
