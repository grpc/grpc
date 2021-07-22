// Copyright 2021 The gRPC Authors
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

#include "test/core/util/resource_user_util.h"

#include "absl/strings/str_format.h"

grpc_resource_user* grpc_resource_user_create_unlimited(
    grpc_resource_quota* resource_quota) {
  if (resource_quota == nullptr) {
    resource_quota = grpc_resource_quota_create("anonymous mock quota");
  } else {
    grpc_resource_quota_ref_internal(resource_quota);
  }
  grpc_resource_user* ru = nullptr;
  ru = grpc_resource_user_create(
      resource_quota, absl::StrFormat("mock_resource_user_%" PRIxPTR,
                                      reinterpret_cast<intptr_t>(&ru))
                          .c_str());
  grpc_resource_quota_unref_internal(resource_quota);
  return ru;
}

grpc_slice_allocator* grpc_slice_allocator_create_unlimited() {
  grpc_slice_allocator_factory* slice_allocator_factory =
      grpc_slice_allocator_factory_create(
          grpc_resource_quota_create("anonymous mock quota"));
  grpc_slice_allocator* slice_allocator =
      grpc_slice_allocator_factory_create_slice_allocator(
          slice_allocator_factory,
          absl::StrFormat("mock_resource_user_from_saf_%p",
                          slice_allocator_factory)
              .c_str());
  grpc_slice_allocator_factory_destroy(slice_allocator_factory);
  return slice_allocator;
}
