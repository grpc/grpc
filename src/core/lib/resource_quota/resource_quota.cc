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

#include "src/core/lib/resource_quota/resource_quota.h"

#include <grpc/support/port_platform.h>

#include <string>
#include <utility>

#include "src/core/channelz/channelz.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/stream_quota.h"
#include "src/core/lib/resource_quota/thread_quota.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/single_set_ptr.h"

namespace grpc_core {

ResourceQuota::ResourceQuota(std::string name)
    : channelz_node_(
          MakeRefCounted<channelz::ResourceQuotaNode>(std::move(name))),
      memory_quota_(MakeMemoryQuota(channelz_node_)),
      thread_quota_(MakeRefCounted<ThreadQuota>()),
      stream_quota_(MakeRefCounted<StreamQuota>()) {}

ResourceQuota::~ResourceQuota() = default;

namespace {
NoDestruct<SingleSetRefCountedPtr<ResourceQuota>> default_resource_quota{};
}  // namespace

ResourceQuotaRefPtr ResourceQuota::Default() {
  return default_resource_quota->GetOrCreate("default_resource_quota");
}

void ResourceQuota::TestOnlyResetDefaultResourceQuota() {
  default_resource_quota->Reset();
}

}  // namespace grpc_core
