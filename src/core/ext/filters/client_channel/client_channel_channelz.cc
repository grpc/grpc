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

#include "src/core/lib/transport/connectivity_state.h"

// IWYU pragma: no_include <type_traits>

namespace grpc_core {
namespace channelz {

SubchannelNode::SubchannelNode(std::string target_address,
                               size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kSubchannel, target_address),
      target_(std::move(target_address)),
      trace_(channel_tracer_max_nodes) {}

SubchannelNode::~SubchannelNode() {}

void SubchannelNode::UpdateConnectivityState(grpc_connectivity_state state) {
  connectivity_state_.store(state, std::memory_order_relaxed);
}

void SubchannelNode::SetChildSocket(RefCountedPtr<SocketNode> socket) {
  MutexLock lock(&socket_mu_);
  child_socket_ = std::move(socket);
}

Json SubchannelNode::RenderJson() {
  // Create and fill the data child.
  grpc_connectivity_state state =
      connectivity_state_.load(std::memory_order_relaxed);
  Json::Object data = {
      {"state",
       Json::Object{
           {"state", ConnectivityStateName(state)},
       }},
      {"target", target_},
  };

  // Fill in the channel trace if applicable
  Json trace_json = trace_.RenderJson();
  if (trace_json.type() != Json::Type::JSON_NULL) {
    data["trace"] = std::move(trace_json);
  }
  // Ask CallCountingHelper to populate call count data.
  call_counter_.PopulateCallCounts(&data);
  // Construct top-level object.
  Json::Object object{
      {"ref",
       Json::Object{
           {"subchannelId", std::to_string(uuid())},
       }},
      {"data", std::move(data)},
  };
  // Populate the child socket.
  RefCountedPtr<SocketNode> child_socket;
  {
    MutexLock lock(&socket_mu_);
    child_socket = child_socket_;
  }
  if (child_socket != nullptr && child_socket->uuid() != 0) {
    object["socketRef"] = Json::Array{
        Json::Object{
            {"socketId", std::to_string(child_socket->uuid())},
            {"name", child_socket->name()},
        },
    };
  }
  return object;
}

}  // namespace channelz
}  // namespace grpc_core
