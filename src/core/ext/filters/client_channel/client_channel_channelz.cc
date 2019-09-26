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

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

#include <grpc/support/string_util.h>

namespace grpc_core {
namespace channelz {

SubchannelNode::SubchannelNode(const char* target_address,
                               size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kSubchannel,
               UniquePtr<char>(gpr_strdup(target_address))),
      target_(UniquePtr<char>(gpr_strdup(target_address))),
      trace_(channel_tracer_max_nodes) {}

SubchannelNode::~SubchannelNode() {}

void SubchannelNode::UpdateConnectivityState(grpc_connectivity_state state) {
  connectivity_state_.Store(state, MemoryOrder::RELAXED);
}

void SubchannelNode::SetChildSocket(RefCountedPtr<SocketNode> socket) {
  MutexLock lock(&socket_mu_);
  child_socket_ = std::move(socket);
}

void SubchannelNode::PopulateConnectivityState(grpc_json* json) {
  grpc_connectivity_state state =
      connectivity_state_.Load(MemoryOrder::RELAXED);
  json = grpc_json_create_child(nullptr, json, "state", nullptr,
                                GRPC_JSON_OBJECT, false);
  grpc_json_create_child(nullptr, json, "state", ConnectivityStateName(state),
                         GRPC_JSON_STRING, false);
}

grpc_json* SubchannelNode::RenderJson() {
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  json_iterator = grpc_json_create_child(json_iterator, json, "ref", nullptr,
                                         GRPC_JSON_OBJECT, false);
  json = json_iterator;
  json_iterator = nullptr;
  json_iterator = grpc_json_add_number_string_child(json, json_iterator,
                                                    "subchannelId", uuid());
  // reset json iterators to top level object
  json = top_level_json;
  json_iterator = nullptr;
  // create and fill the data child.
  grpc_json* data = grpc_json_create_child(json_iterator, json, "data", nullptr,
                                           GRPC_JSON_OBJECT, false);
  json = data;
  json_iterator = nullptr;
  PopulateConnectivityState(json);
  GPR_ASSERT(target_.get() != nullptr);
  grpc_json_create_child(nullptr, json, "target", target_.get(),
                         GRPC_JSON_STRING, false);
  // fill in the channel trace if applicable
  grpc_json* trace_json = trace_.RenderJson();
  if (trace_json != nullptr) {
    trace_json->key = "trace";  // this object is named trace in channelz.proto
    grpc_json_link_child(json, trace_json, nullptr);
  }
  // ask CallCountingHelper to populate trace and call count data.
  call_counter_.PopulateCallCounts(json);
  json = top_level_json;
  // populate the child socket.
  RefCountedPtr<SocketNode> child_socket;
  {
    MutexLock lock(&socket_mu_);
    child_socket = child_socket_;
  }
  if (child_socket != nullptr && child_socket->uuid() != 0) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "socketRef", nullptr, GRPC_JSON_ARRAY, false);
    json_iterator = grpc_json_create_child(json_iterator, array_parent, nullptr,
                                           nullptr, GRPC_JSON_OBJECT, false);
    grpc_json* sibling_iterator = grpc_json_add_number_string_child(
        json_iterator, nullptr, "socketId", child_socket->uuid());
    grpc_json_create_child(sibling_iterator, json_iterator, "name",
                           child_socket->name(), GRPC_JSON_STRING, false);
  }
  return top_level_json;
}

}  // namespace channelz
}  // namespace grpc_core
