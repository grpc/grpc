/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCXX_RESOURCE_QUOTA_H
#define GRPCXX_RESOURCE_QUOTA_H

struct grpc_resource_quota;

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/grpc_library.h>

namespace grpc {

/// ResourceQuota represents a bound on memory usage by the gRPC library.
/// A ResourceQuota can be attached to a server (via ServerBuilder), or a client
/// channel (via ChannelArguments). gRPC will attempt to keep memory used by
/// all attached entities below the ResourceQuota bound.
class ResourceQuota final : private GrpcLibraryCodegen {
 public:
  explicit ResourceQuota(const grpc::string& name);
  ResourceQuota();
  ~ResourceQuota();

  /// Resize this ResourceQuota to a new size. If new_size is smaller than the
  /// current size of the pool, memory usage will be monotonically decreased
  /// until it falls under new_size. No time bound is given for this to occur
  /// however.
  ResourceQuota& Resize(size_t new_size);

  grpc_resource_quota* c_resource_quota() const { return impl_; }

 private:
  ResourceQuota(const ResourceQuota& rhs);
  ResourceQuota& operator=(const ResourceQuota& rhs);

  grpc_resource_quota* const impl_;
};

}  // namespace grpc

#endif  // GRPCXX_RESOURCE_QUOTA_H
