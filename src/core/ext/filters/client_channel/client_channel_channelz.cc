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

#include <string>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

#include <grpc/support/string_util.h>

using nlohmann::json;

namespace grpc_core {
namespace channelz {

SubchannelNode::SubchannelNode(std::string target_address,
                               size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kSubchannel, target_address),
      target_(std::move(target_address)),
      trace_(channel_tracer_max_nodes) {}

SubchannelNode::~SubchannelNode() {}

void SubchannelNode::UpdateConnectivityState(grpc_connectivity_state state) {
  connectivity_state_.Store(state, MemoryOrder::RELAXED);
}

void SubchannelNode::SetChildSocket(RefCountedPtr<SocketNode> socket) {
  MutexLock lock(&socket_mu_);
  child_socket_ = std::move(socket);
}

json SubchannelNode::RenderJson() {
  json j = {
      {"ref", {{"subchannelId", std::to_string(uuid())}}},
  };
  // Create and fill the data child.
  grpc_connectivity_state state =
      connectivity_state_.Load(MemoryOrder::RELAXED);
  j["data"] = {
      {
          "state",
          {{"state", ConnectivityStateName(state)}},
      },
      {"target", target_},
  };
  // Fill in the channel trace if applicable.
  json trace_json = trace_.RenderJson();
  if (!trace_json.is_null()) j["data"]["trace"] = std::move(trace_json);
  // Ask CallCountingHelper to populate trace and call count data.
  call_counter_.PopulateCallCounts(&j["data"]);
  // Populate the child socket.
  RefCountedPtr<SocketNode> child_socket;
  {
    MutexLock lock(&socket_mu_);
    child_socket = child_socket_;
  }
  if (child_socket != nullptr && child_socket->uuid() != 0) {
    j["socketRef"] = json::array();
    j["socketRef"].push_back({
        {"socketId", std::to_string(child_socket->uuid())},
        {"name", child_socket->name()},
    });
  }
  return j;
}

}  // namespace channelz
}  // namespace grpc_core
