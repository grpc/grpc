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

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/cpp_impl_of.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/thread_quota.h"

namespace grpc_core {

class ResourceQuota;
using ResourceQuotaRefPtr = RefCountedPtr<ResourceQuota>;

class ResourceQuota : public RefCounted<ResourceQuota>,
                      public CppImplOf<ResourceQuota, grpc_resource_quota> {
 public:
  explicit ResourceQuota(std::string name);
  ~ResourceQuota() override;

  ResourceQuota(const ResourceQuota&) = delete;
  ResourceQuota& operator=(const ResourceQuota&) = delete;

  static absl::string_view ChannelArgName() { return GRPC_ARG_RESOURCE_QUOTA; }

  MemoryQuotaRefPtr memory_quota() { return memory_quota_; }

  const RefCountedPtr<ThreadQuota>& thread_quota() { return thread_quota_; }

  // The default global resource quota
  static ResourceQuotaRefPtr Default();

  static int ChannelArgsCompare(const ResourceQuota* a,
                                const ResourceQuota* b) {
    return QsortCompare(a, b);
  }

 private:
  MemoryQuotaRefPtr memory_quota_;
  RefCountedPtr<ThreadQuota> thread_quota_;
};

inline ResourceQuotaRefPtr MakeResourceQuota(std::string name) {
  return MakeRefCounted<ResourceQuota>(std::move(name));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOURCE_QUOTA_RESOURCE_QUOTA_H
