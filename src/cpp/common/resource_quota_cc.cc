/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/resource_quota.h>

namespace grpc {

ResourceQuota::ResourceQuota() : impl_(grpc_resource_quota_create(nullptr)) {}

ResourceQuota::ResourceQuota(const std::string& name)
    : impl_(grpc_resource_quota_create(name.c_str())) {}

ResourceQuota::~ResourceQuota() { grpc_resource_quota_unref(impl_); }

ResourceQuota& ResourceQuota::Resize(size_t new_size) {
  grpc_resource_quota_resize(impl_, new_size);
  return *this;
}

ResourceQuota& ResourceQuota::SetMaxThreads(int new_max_threads) {
  grpc_resource_quota_set_max_threads(impl_, new_max_threads);
  return *this;
}
}  // namespace grpc
