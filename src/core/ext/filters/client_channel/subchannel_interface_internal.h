//
// Copyright 2022 gRPC authors.
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
//

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_INTERNAL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/ext/filters/client_channel/subchannel_interface.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/iomgr/work_serializer.h"

namespace grpc_core {

// Interface for watching data of a particular type for this subchannel.
// Implementations will generally define their own type-specific methods.
// FIXME: does this actually need to be DualRefCounted?  Or even
// RefCounted at all?  Maybe it can just be single-owner.  Need to
// consider how LB policy will set this as it creates a new subchannel
// list for each address list update from its parent.
class SubchannelInterface::DataWatcherInterface
    : public DualRefCounted<SubchannelInterface::DataWatcherInterface> {
 public:
  // Tells the watcher which subchannel to register itself with.
  virtual void SetSubchannel(
      Subchannel* subchannel,
      std::shared_ptr<WorkSerializer> work_serializer) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_INTERNAL_H
