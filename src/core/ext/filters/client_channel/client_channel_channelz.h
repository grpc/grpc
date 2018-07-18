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
      grpc_channel* channel, size_t channel_tracer_max_nodes);

  // Override this functionality since client_channels have a notion of
  // channel connectivity.
  void PopulateConnectivityState(grpc_json* json) override;

  // Override this functionality since client_channels have subchannels
  void PopulateChildRefs(grpc_json* json) override;

  // Helper to create a channel arg to ensure this type of ChannelNode is
  // created.
  static grpc_arg CreateChannelArg();

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW
  ClientChannelNode(grpc_channel* channel, size_t channel_tracer_max_nodes);
  virtual ~ClientChannelNode() {}

 private:
  grpc_channel_element* client_channel_;
};

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_CHANNELZ_H */
