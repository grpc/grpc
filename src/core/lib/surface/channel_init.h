//
//
// Copyright 2016 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INIT_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INIT_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <utility>
#include <vector>

#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_stack_type.h"

#define GRPC_CHANNEL_INIT_BUILTIN_PRIORITY 10000

/// This module provides a way for plugins (and the grpc core library itself)
/// to register mutators for channel stacks.
/// It also provides a universal entry path to run those mutators to build
/// a channel stack for various subsystems.

namespace grpc_core {

class ChannelInit {
 public:
  /// One stage of mutation: call functions against \a builder to influence the
  /// finally constructed channel stack
  using Stage = std::function<bool(ChannelStackBuilder* builder)>;

  class Builder {
   public:
    /// Register one stage of mutators.
    /// Stages are run in priority order (lowest to highest), and then in
    /// registration order (in the case of a tie).
    /// Stages are registered against one of the pre-determined channel stack
    /// types.
    /// If the channel stack type is GRPC_CLIENT_SUBCHANNEL, the caller should
    /// ensure that subchannels with different filter lists will always have
    /// different channel args. This requires setting a channel arg in case the
    /// registration function relies on some condition other than channel args
    /// to decide whether to add a filter or not.
    void RegisterStage(grpc_channel_stack_type type, int priority, Stage stage);

    /// Finalize registration. No more calls to grpc_channel_init_register_stage
    /// are allowed.
    ChannelInit Build();

   private:
    struct Slot {
      Slot(Stage stage, int priority)
          : stage(std::move(stage)), priority(priority) {}
      Stage stage;
      int priority;
    };
    std::vector<Slot> slots_[GRPC_NUM_CHANNEL_STACK_TYPES];
  };

  /// Construct a channel stack of some sort: see channel_stack.h for details
  /// \a builder is the channel stack builder to build into.
  bool CreateStack(ChannelStackBuilder* builder) const;

 private:
  std::vector<Stage> slots_[GRPC_NUM_CHANNEL_STACK_TYPES];
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INIT_H
