// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_RESOLVER_RESOLVER_CHANNEL_H
#define GRPC_SRC_CORE_LIB_RESOLVER_RESOLVER_CHANNEL_H

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/promise/observable.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/transport/call_filters.h"
#include "src/core/lib/transport/channel.h"

namespace grpc_core {

class ResolverChannel final : public Channel {
 public:
  absl::StatusOr<RefCountedPtr<ResolverChannel>> Create(ChannelArgs args);

  CallInitiator CreateCall(ClientMetadataHandle metadata,
                           Arena* arena) override;

 private:
  class ResolvedStack : public RefCounted<ResolvedStack> {
   public:
    void StartCall(CallHandler handler);

   private:
    RefCountedPtr<CallFilters::Stack> call_stack_;
  };

  class ResolverResultHandler;
  static absl::StatusOr<RefCountedPtr<ResolvedStack>>
  CreateResolvedStackFromResolverResult(Resolver::Result result);

  ResolverChannel(const ChannelArgs& args,
                  std::shared_ptr<WorkSerializer> work_serializer,
                  OrphanablePtr<Resolver> resolver);
  void UpdateResolverResultLocked(Resolver::Result result);

  Observable<RefCountedPtr<ResolvedStack>> resolved_stack_;
  const std::shared_ptr<WorkSerializer> work_serializer_;
  const OrphanablePtr<Resolver> resolver_;
};

}  // namespace grpc_core

#endif
