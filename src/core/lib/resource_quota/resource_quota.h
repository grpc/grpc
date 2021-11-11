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

#ifndef GRPC_CORE_LIB_RESOURCE_QUOTA_RESOURCE_QUOTA_H
#define GRPC_CORE_LIB_RESOURCE_QUOTA_RESOURCE_QUOTA_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/thread_quota.h"

namespace grpc_core {

class ResourceQuota : public RefCounted<ResourceQuota> {
 public:
  ResourceQuota();
  ~ResourceQuota() override;

  ResourceQuota(const ResourceQuota&) = delete;
  ResourceQuota& operator=(const ResourceQuota&) = delete;

  std::shared_ptr<MemoryQuota> memory_quota() { return memory_quota_; }

  const RefCountedPtr<ThreadQuota>& thread_quota() { return thread_quota_; }

 private:
  std::shared_ptr<MemoryQuota> memory_quota_;
  RefCountedPtr<ThreadQuota> thread_quota_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOURCE_QUOTA_RESOURCE_QUOTA_H
