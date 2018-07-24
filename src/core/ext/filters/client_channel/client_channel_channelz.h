/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_CHANNELZ_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_CHANNELZ_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/inlined_vector.h"

typedef struct grpc_subchannel grpc_subchannel;

namespace grpc_core {

// TODO(ncteisen), this only contains the uuids of the children for now,
// since that is all that is strictly needed. In a future enhancement we will
// add human readable names as in the channelz.proto
typedef InlinedVector<intptr_t, 10> ChildRefsList;

namespace channelz {

// Subtype of ChannelNode that overrides and provides client_channel specific
// functionality like querying for connectivity_state and subchannel data.
class ClientChannelNode : public ChannelNode {
 public:
  static RefCountedPtr<ChannelNode> MakeClientChannelNode(
      grpc_channel* channel, size_t channel_tracer_max_nodes,
      bool is_top_level_channel);

  ClientChannelNode(grpc_channel* channel, size_t channel_tracer_max_nodes,
                    bool is_top_level_channel);
  virtual ~ClientChannelNode() {}

  grpc_json* RenderJson() override;

  // Helper to create a channel arg to ensure this type of ChannelNode is
  // created.
  static grpc_arg CreateChannelArg();

 private:
  grpc_channel_element* client_channel_;

  // helpers
  void PopulateConnectivityState(grpc_json* json);
  void PopulateChildRefs(grpc_json* json);
};

// Handles channelz bookkeeping for sockets
class SubchannelNode : public BaseNode {
 public:
  SubchannelNode(grpc_subchannel* subchannel, size_t channel_tracer_max_nodes);
  ~SubchannelNode() override;

  void MarkSubchannelDestroyed() {
    GPR_ASSERT(subchannel_ != nullptr);
    subchannel_ = nullptr;
  }

  grpc_json* RenderJson() override;

  CallCountingAndTracingNode* counter_and_tracer() {
    return &counter_and_tracer_;
  }

 private:
  grpc_subchannel* subchannel_;
  UniquePtr<char> target_;
  CallCountingAndTracingNode counter_and_tracer_;

  void PopulateConnectivityState(grpc_json* json);
};

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_CHANNELZ_H */
