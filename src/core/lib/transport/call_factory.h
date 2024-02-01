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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_CHANNEL_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_CHANNEL_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/call_size_estimator.h"
#include "src/core/lib/transport/call_spine.h"

namespace grpc_core {

class CallFactory : public RefCounted<CallFactory> {
 public:
  explicit CallFactory(const ChannelArgs& args);

  Arena* CreateArena();
  void DestroyArena(Arena* arena);

  virtual CallInitiator CreateCall(ClientMetadataHandle md, Arena* arena) = 0;

 private:
  CallSizeEstimator call_size_estimator_;
  MemoryAllocator allocator_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CHANNEL_H
