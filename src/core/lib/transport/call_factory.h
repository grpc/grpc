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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_CALL_FACTORY_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_CALL_FACTORY_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/call_size_estimator.h"
#include "src/core/lib/transport/call_spine.h"

namespace grpc_core {

// CallFactory creates calls.
class CallFactory : public RefCounted<CallFactory> {
 public:
  explicit CallFactory(const ChannelArgs& args);

  // Create an arena for a call.
  // We do this as a separate step so that servers can create arenas without
  // creating the call into it - in the case that we have a HTTP/2 rapid reset
  // like attack this saves a lot of cpu time.
  Arena* CreateArena();
  // Destroy an arena created by CreateArena.
  // Updates the call size estimator so that we always create arenas of about
  // the right size.
  void DestroyArena(Arena* arena);

  // Create a call. The call will be created in the given arena.
  // It is the CallFactory's responsibility to ensure that the CallHandler
  // associated with the call is eventually handled by something (typically a
  // CallDestination, but this is not strictly required).
  virtual CallInitiator CreateCall(ClientMetadataHandle md, Arena* arena) = 0;

 private:
  CallSizeEstimator call_size_estimator_;
  MemoryAllocator allocator_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_FACTORY_H
