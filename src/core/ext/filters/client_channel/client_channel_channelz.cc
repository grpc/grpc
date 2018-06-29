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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/lib/gpr/useful.h"

namespace grpc_core {
namespace channelz {

static void* client_channel_channelz_copy(void* p) { return p; }

static void client_channel_channelz_destroy(void* p) {}

static int client_channel_channelz_cmp(void* a, void* b) {
  return GPR_ICMP(a, b);
}

static const grpc_arg_pointer_vtable client_channel_channelz_vtable = {
    client_channel_channelz_copy, client_channel_channelz_destroy,
    client_channel_channelz_cmp};

bool ClientChannelNode::GetConnectivityState(grpc_connectivity_state* state) {
  if (channel()) {
    *state = grpc_channel_check_connectivity_state(channel(), false);
  } else {
    *state = GRPC_CHANNEL_SHUTDOWN;
  }
  return true;
}

grpc_arg ClientChannelNode::CreateArg() {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CHANNELZ_CHANNEL_NODE_CREATION_FUNC),
      reinterpret_cast<void*>(MakeClientChannelNode),
      &client_channel_channelz_vtable);
}

RefCountedPtr<ChannelNode> MakeClientChannelNode(
    grpc_channel* channel, size_t channel_tracer_max_nodes) {
  return RefCountedPtr<ChannelNode>(
      New<ClientChannelNode>(channel, channel_tracer_max_nodes));
}

}  // namespace channelz
}  // namespace grpc_core
