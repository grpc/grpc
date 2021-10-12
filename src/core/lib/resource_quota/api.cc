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

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

ResourceQuotaPtr ResourceQuotaFromChannelArgs(const grpc_channel_args* args) {
  return grpc_channel_args_find_pointer<ResourceQuota>(args,
                                                       GRPC_ARG_RESOURCE_QUOTA)
      ->Ref();
}

namespace {
grpc_arg MakeArg(ResourceQuota* quota) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA);
  arg.value.pointer.p = quota;
  arg.value.pointer.vtable = grpc_resource_quota_arg_vtable();
  return arg;
}
}  // namespace

grpc_channel_args* EnsureResourceQuotaInChannelArgs(
    const grpc_channel_args* args) {
  if (grpc_channel_args_find(args, GRPC_ARG_RESOURCE_QUOTA) != nullptr) {
    return grpc_channel_args_copy(args);
  }
  auto resource_quota = MakeResourceQuota();
  auto new_arg = MakeArg(resource_quota.get());
  return grpc_channel_args_copy_and_add(args, &new_arg, 1);
}

grpc_channel_args* ChannelArgsWrappingResourceQuota(
    ResourceQuotaPtr resource_quota) {
  auto new_arg = MakeArg(resource_quota.get());
  return grpc_channel_args_copy_and_add(nullptr, &new_arg, 1);
}

}  // namespace grpc_core

extern "C" const grpc_arg_pointer_vtable* grpc_resource_quota_arg_vtable() {
  static const grpc_arg_pointer_vtable vtable = {
      // copy
      [](void* p) -> void* {
        return static_cast<grpc_core::ResourceQuota*>(p)->Ref().release();
      },
      // destroy
      [](void* p) { static_cast<grpc_core::ResourceQuota*>(p)->Unref(); },
      // compare
      [](void* p, void* q) { return grpc_core::QsortCompare(p, q); }};
  return &vtable;
}

extern "C" grpc_resource_quota* grpc_resource_quota_create(const char*) {
  return reinterpret_cast<grpc_resource_quota*>(new grpc_core::ResourceQuota());
}

extern "C" void grpc_resource_quota_ref(grpc_resource_quota* rq) {
  reinterpret_cast<grpc_core::ResourceQuota*>(rq)->Ref().release();
}

extern "C" void grpc_resource_quota_unref(grpc_resource_quota* rq) {
  reinterpret_cast<grpc_core::ResourceQuota*>(rq)->Unref();
}

extern "C" void grpc_resource_quota_resize(grpc_resource_quota* rq,
                                           size_t new_size) {
  reinterpret_cast<grpc_core::ResourceQuota*>(rq)->memory_quota()->SetSize(
      new_size);
}

extern "C" void grpc_resource_quota_set_max_threads(grpc_resource_quota* rq,
                                                    int new_max_threads) {
  reinterpret_cast<grpc_core::ResourceQuota*>(rq)->thread_quota()->SetMax(
      new_max_threads);
}
