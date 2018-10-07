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
namespace {

void* client_channel_channelz_copy(void* p) { return p; }

void client_channel_channelz_destroy(void* p) {}

int client_channel_channelz_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

}  // namespace

static const grpc_arg_pointer_vtable client_channel_channelz_vtable = {
    client_channel_channelz_copy, client_channel_channelz_destroy,
    client_channel_channelz_cmp};

ClientChannelNode::ClientChannelNode(grpc_channel* channel,
                                     size_t channel_tracer_max_nodes,
                                     bool is_top_level_channel)
    : ChannelNode(channel, channel_tracer_max_nodes, is_top_level_channel) {
  client_channel_ =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  GPR_ASSERT(client_channel_->filter == &grpc_client_channel_filter);
}

void ClientChannelNode::PopulateConnectivityState(grpc_json* json) {
  grpc_connectivity_state state;
  if (ChannelIsDestroyed()) {
    state = GRPC_CHANNEL_SHUTDOWN;
  } else {
    state =
        grpc_client_channel_check_connectivity_state(client_channel_, false);
  }
  json = grpc_json_create_child(nullptr, json, "state", nullptr,
                                GRPC_JSON_OBJECT, false);
  grpc_json_create_child(nullptr, json, "state",
                         grpc_connectivity_state_name(state), GRPC_JSON_STRING,
                         false);
}

void ClientChannelNode::PopulateChildRefs(grpc_json* json) {
  ChildRefsList child_subchannels;
  ChildRefsList child_channels;
  grpc_json* json_iterator = nullptr;
  grpc_client_channel_populate_child_refs(client_channel_, &child_subchannels,
                                          &child_channels);
  if (!child_subchannels.empty()) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "subchannelRef", nullptr, GRPC_JSON_ARRAY, false);
    for (size_t i = 0; i < child_subchannels.size(); ++i) {
      json_iterator =
          grpc_json_create_child(json_iterator, array_parent, nullptr, nullptr,
                                 GRPC_JSON_OBJECT, false);
      grpc_json_add_number_string_child(json_iterator, nullptr, "subchannelId",
                                        child_subchannels[i]);
    }
  }
  if (!child_channels.empty()) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "channelRef", nullptr, GRPC_JSON_ARRAY, false);
    json_iterator = nullptr;
    for (size_t i = 0; i < child_channels.size(); ++i) {
      json_iterator =
          grpc_json_create_child(json_iterator, array_parent, nullptr, nullptr,
                                 GRPC_JSON_OBJECT, false);
      grpc_json_add_number_string_child(json_iterator, nullptr, "channelId",
                                        child_channels[i]);
    }
  }
}

grpc_arg ClientChannelNode::CreateChannelArg() {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CHANNELZ_CHANNEL_NODE_CREATION_FUNC),
      reinterpret_cast<void*>(MakeClientChannelNode),
      &client_channel_channelz_vtable);
}

RefCountedPtr<ChannelNode> ClientChannelNode::MakeClientChannelNode(
    grpc_channel* channel, size_t channel_tracer_max_nodes,
    bool is_top_level_channel) {
  return MakeRefCounted<ClientChannelNode>(channel, channel_tracer_max_nodes,
                                           is_top_level_channel);
}

SubchannelNode::SubchannelNode(grpc_subchannel* subchannel,
                               size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kSubchannel),
      subchannel_(subchannel),
      target_(
          UniquePtr<char>(gpr_strdup(grpc_subchannel_get_target(subchannel_)))),
      trace_(channel_tracer_max_nodes) {}

SubchannelNode::~SubchannelNode() {}

void SubchannelNode::PopulateConnectivityState(grpc_json* json) {
  grpc_connectivity_state state;
  if (subchannel_ == nullptr) {
    state = GRPC_CHANNEL_SHUTDOWN;
  } else {
    state = grpc_subchannel_check_connectivity(subchannel_, nullptr);
  }
  json = grpc_json_create_child(nullptr, json, "state", nullptr,
                                GRPC_JSON_OBJECT, false);
  grpc_json_create_child(nullptr, json, "state",
                         grpc_connectivity_state_name(state), GRPC_JSON_STRING,
                         false);
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
  intptr_t socket_uuid = grpc_subchannel_get_child_socket_uuid(subchannel_);
  if (socket_uuid != 0) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "socketRef", nullptr, GRPC_JSON_ARRAY, false);
    json_iterator = grpc_json_create_child(json_iterator, array_parent, nullptr,
                                           nullptr, GRPC_JSON_OBJECT, false);
    grpc_json_add_number_string_child(json_iterator, nullptr, "socketId",
                                      socket_uuid);
  }
  return top_level_json;
}

}  // namespace channelz
}  // namespace grpc_core
