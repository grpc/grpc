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

#include "src/core/lib/resource_quota/api.h"

#include <stdint.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/resource_quota/thread_quota.h"

namespace grpc_core {

ResourceQuotaRefPtr ResourceQuotaFromChannelArgs(
    const grpc_channel_args* args) {
  return grpc_channel_args_find_pointer<ResourceQuota>(args,
                                                       GRPC_ARG_RESOURCE_QUOTA)
      ->Ref();
}

ResourceQuotaRefPtr ResourceQuotaFromEndpointConfig(
    const grpc_event_engine::experimental::EndpointConfig& config) {
  void* value = config.GetVoidPointer(GRPC_ARG_RESOURCE_QUOTA);
  if (value != nullptr) {
    return reinterpret_cast<ResourceQuota*>(value)->Ref();
  }
  return nullptr;
}

ChannelArgs EnsureResourceQuotaInChannelArgs(const ChannelArgs& args) {
  if (args.GetObject<ResourceQuota>() != nullptr) return args;
  // If there's no existing quota, add it to the default one - shared between
  // all channel args declared thusly. This prevents us from accidentally not
  // sharing subchannels due to their channel args not specifying a quota.
  return args.SetObject(ResourceQuota::Default());
}

void RegisterResourceQuota(CoreConfiguration::Builder* builder) {
  builder->channel_args_preconditioning()->RegisterStage(
      EnsureResourceQuotaInChannelArgs);
}

}  // namespace grpc_core

extern "C" const grpc_arg_pointer_vtable* grpc_resource_quota_arg_vtable() {
  return grpc_core::ChannelArgTypeTraits<grpc_core::ResourceQuota>::VTable();
}

extern "C" grpc_resource_quota* grpc_resource_quota_create(const char* name) {
  static std::atomic<uintptr_t> anonymous_counter{0};
  std::string quota_name =
      name == nullptr
          ? absl::StrCat("anonymous-quota-", anonymous_counter.fetch_add(1))
          : name;
  return (new grpc_core::ResourceQuota(std::move(quota_name)))->c_ptr();
}

extern "C" void grpc_resource_quota_ref(grpc_resource_quota* resource_quota) {
  grpc_core::ResourceQuota::FromC(resource_quota)->Ref().release();
}

extern "C" void grpc_resource_quota_unref(grpc_resource_quota* resource_quota) {
  grpc_core::ResourceQuota::FromC(resource_quota)->Unref();
}

extern "C" void grpc_resource_quota_resize(grpc_resource_quota* resource_quota,
                                           size_t new_size) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::ResourceQuota::FromC(resource_quota)
      ->memory_quota()
      ->SetSize(new_size);
}

extern "C" void grpc_resource_quota_set_max_threads(
    grpc_resource_quota* resource_quota, int new_max_threads) {
  grpc_core::ResourceQuota::FromC(resource_quota)
      ->thread_quota()
      ->SetMax(new_max_threads);
}
