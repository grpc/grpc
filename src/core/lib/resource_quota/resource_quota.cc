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

#include <grpc/support/port_platform.h>

#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {

ResourceQuota::ResourceQuota(std::string name)
    : memory_quota_(MakeMemoryQuota(std::move(name))),
      thread_quota_(MakeRefCounted<ThreadQuota>()) {}

ResourceQuota::~ResourceQuota() = default;

ResourceQuotaRefPtr ResourceQuota::Default() {
  static auto default_resource_quota =
      MakeResourceQuota("default_resource_quota").release();
  return default_resource_quota->Ref();
}

}  // namespace grpc_core
