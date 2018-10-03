/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/channel/channelz.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {
namespace channelz {

BaseNode::BaseNode(EntityType type)
    : type_(type), uuid_(ChannelzRegistry::Register(this)) {}

BaseNode::~BaseNode() { ChannelzRegistry::Unregister(uuid_); }

char* BaseNode::RenderJsonString() {
  grpc_json* json = RenderJson();
  GPR_ASSERT(json != nullptr);
  char* json_str = grpc_json_dump_to_string(json, 0);
  grpc_json_destroy(json);
  return json_str;
}

CallCountingHelper::CallCountingHelper() {
  gpr_atm_no_barrier_store(&last_call_started_millis_,
                           (gpr_atm)ExecCtx::Get()->Now());
}

CallCountingHelper::~CallCountingHelper() {}

void CallCountingHelper::RecordCallStarted() {
  gpr_atm_no_barrier_fetch_add(&calls_started_, static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(&last_call_started_millis_,
                           (gpr_atm)ExecCtx::Get()->Now());
}

void CallCountingHelper::PopulateCallCounts(grpc_json* json) {
  grpc_json* json_iterator = nullptr;
  if (calls_started_ != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "callsStarted", calls_started_);
  }
  if (calls_succeeded_ != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "callsSucceeded", calls_succeeded_);
  }
  if (calls_failed_) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "callsFailed", calls_failed_);
  }
  if (calls_started_ != 0) {
    gpr_timespec ts =
        grpc_millis_to_timespec(last_call_started_millis_, GPR_CLOCK_REALTIME);
    json_iterator =
        grpc_json_create_child(json_iterator, json, "lastCallStartedTimestamp",
                               gpr_format_timespec(ts), GRPC_JSON_STRING, true);
  }
}

ChannelNode::ChannelNode(grpc_channel* channel, size_t channel_tracer_max_nodes,
                         bool is_top_level_channel)
    : BaseNode(is_top_level_channel ? EntityType::kTopLevelChannel
                                    : EntityType::kInternalChannel),
      channel_(channel),
      target_(UniquePtr<char>(grpc_channel_get_target(channel_))),
      trace_(channel_tracer_max_nodes) {}

ChannelNode::~ChannelNode() {}

grpc_json* ChannelNode::RenderJson() {
  // We need to track these three json objects to build our object
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  // create and fill the ref child
  json_iterator = grpc_json_create_child(json_iterator, json, "ref", nullptr,
                                         GRPC_JSON_OBJECT, false);
  json = json_iterator;
  json_iterator = nullptr;
  json_iterator = grpc_json_add_number_string_child(json, json_iterator,
                                                    "channelId", uuid());
  // reset json iterators to top level object
  json = top_level_json;
  json_iterator = nullptr;
  // create and fill the data child.
  grpc_json* data = grpc_json_create_child(json_iterator, json, "data", nullptr,
                                           GRPC_JSON_OBJECT, false);
  json = data;
  json_iterator = nullptr;
  // template method. Child classes may override this to add their specific
  // functionality.
  PopulateConnectivityState(json);
  // populate the target.
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
  // template method. Child classes may override this to add their specific
  // functionality.
  PopulateChildRefs(json);
  return top_level_json;
}

RefCountedPtr<ChannelNode> ChannelNode::MakeChannelNode(
    grpc_channel* channel, size_t channel_tracer_max_nodes,
    bool is_top_level_channel) {
  return MakeRefCounted<grpc_core::channelz::ChannelNode>(
      channel, channel_tracer_max_nodes, is_top_level_channel);
}

ServerNode::ServerNode(grpc_server* server, size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kServer),
      server_(server),
      trace_(channel_tracer_max_nodes) {}

ServerNode::~ServerNode() {}

char* ServerNode::RenderServerSockets(intptr_t start_socket_id) {
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  ChildRefsList socket_refs;
  // uuids index into entities one-off (idx 0 is really uuid 1, since 0 is
  // reserved). However, we want to support requests coming in with
  // start_server_id=0, which signifies "give me everything."
  size_t start_idx = start_socket_id == 0 ? 0 : start_socket_id - 1;
  grpc_server_populate_server_sockets(server_, &socket_refs, start_idx);
  if (!socket_refs.empty()) {
    // create list of socket refs
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "socketRef", nullptr, GRPC_JSON_ARRAY, false);
    for (size_t i = 0; i < socket_refs.size(); ++i) {
      json_iterator =
          grpc_json_create_child(json_iterator, array_parent, nullptr, nullptr,
                                 GRPC_JSON_OBJECT, false);
      grpc_json_add_number_string_child(json_iterator, nullptr, "socketId",
                                        socket_refs[i]);
    }
  }
  // For now we do not have any pagination rules. In the future we could
  // pick a constant for max_channels_sent for a GetServers request.
  // Tracking: https://github.com/grpc/grpc/issues/16019.
  json_iterator = grpc_json_create_child(nullptr, json, "end", nullptr,
                                         GRPC_JSON_TRUE, false);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

grpc_json* ServerNode::RenderJson() {
  // We need to track these three json objects to build our object
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  // create and fill the ref child
  json_iterator = grpc_json_create_child(json_iterator, json, "ref", nullptr,
                                         GRPC_JSON_OBJECT, false);
  json = json_iterator;
  json_iterator = nullptr;
  json_iterator = grpc_json_add_number_string_child(json, json_iterator,
                                                    "serverId", uuid());
  // reset json iterators to top level object
  json = top_level_json;
  json_iterator = nullptr;
  // create and fill the data child.
  grpc_json* data = grpc_json_create_child(json_iterator, json, "data", nullptr,
                                           GRPC_JSON_OBJECT, false);
  json = data;
  json_iterator = nullptr;
  // fill in the channel trace if applicable
  grpc_json* trace_json = trace_.RenderJson();
  if (trace_json != nullptr) {
    trace_json->key = "trace";  // this object is named trace in channelz.proto
    grpc_json_link_child(json, trace_json, nullptr);
  }
  // ask CallCountingHelper to populate trace and call count data.
  call_counter_.PopulateCallCounts(json);
  return top_level_json;
}

SocketNode::SocketNode() : BaseNode(EntityType::kSocket) {}

void SocketNode::RecordStreamStartedFromLocal() {
  gpr_atm_no_barrier_fetch_add(&streams_started_, static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(&last_local_stream_created_millis_,
                           (gpr_atm)ExecCtx::Get()->Now());
}

void SocketNode::RecordStreamStartedFromRemote() {
  gpr_atm_no_barrier_fetch_add(&streams_started_, static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(&last_remote_stream_created_millis_,
                           (gpr_atm)ExecCtx::Get()->Now());
}

void SocketNode::RecordMessagesSent(uint32_t num_sent) {
  gpr_atm_no_barrier_fetch_add(&messages_sent_, static_cast<gpr_atm>(num_sent));
  gpr_atm_no_barrier_store(&last_message_sent_millis_,
                           (gpr_atm)ExecCtx::Get()->Now());
}

void SocketNode::RecordMessageReceived() {
  gpr_atm_no_barrier_fetch_add(&messages_received_, static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(&last_message_received_millis_,
                           (gpr_atm)ExecCtx::Get()->Now());
}

grpc_json* SocketNode::RenderJson() {
  // We need to track these three json objects to build our object
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  // create and fill the ref child
  json_iterator = grpc_json_create_child(json_iterator, json, "ref", nullptr,
                                         GRPC_JSON_OBJECT, false);
  json = json_iterator;
  json_iterator = nullptr;
  json_iterator = grpc_json_add_number_string_child(json, json_iterator,
                                                    "socketId", uuid());
  // reset json iterators to top level object
  json = top_level_json;
  json_iterator = nullptr;
  // create and fill the data child.
  grpc_json* data = grpc_json_create_child(json_iterator, json, "data", nullptr,
                                           GRPC_JSON_OBJECT, false);
  json = data;
  json_iterator = nullptr;
  gpr_timespec ts;
  if (streams_started_ != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "streamsStarted", streams_started_);
    if (last_local_stream_created_millis_ != 0) {
      ts = grpc_millis_to_timespec(last_local_stream_created_millis_,
                                   GPR_CLOCK_REALTIME);
      json_iterator = grpc_json_create_child(
          json_iterator, json, "lastLocalStreamCreatedTimestamp",
          gpr_format_timespec(ts), GRPC_JSON_STRING, true);
    }
    if (last_remote_stream_created_millis_ != 0) {
      ts = grpc_millis_to_timespec(last_remote_stream_created_millis_,
                                   GPR_CLOCK_REALTIME);
      json_iterator = grpc_json_create_child(
          json_iterator, json, "lastRemoteStreamCreatedTimestamp",
          gpr_format_timespec(ts), GRPC_JSON_STRING, true);
    }
  }
  if (streams_succeeded_ != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "streamsSucceeded", streams_succeeded_);
  }
  if (streams_failed_) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "streamsFailed", streams_failed_);
  }
  if (messages_sent_ != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "messagesSent", messages_sent_);
    ts = grpc_millis_to_timespec(last_message_sent_millis_, GPR_CLOCK_REALTIME);
    json_iterator =
        grpc_json_create_child(json_iterator, json, "lastMessageSentTimestamp",
                               gpr_format_timespec(ts), GRPC_JSON_STRING, true);
  }
  if (messages_received_ != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "messagesReceived", messages_received_);
    ts = grpc_millis_to_timespec(last_message_received_millis_,
                                 GPR_CLOCK_REALTIME);
    json_iterator = grpc_json_create_child(
        json_iterator, json, "lastMessageReceivedTimestamp",
        gpr_format_timespec(ts), GRPC_JSON_STRING, true);
  }
  if (keepalives_sent_ != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "keepAlivesSent", keepalives_sent_);
  }
  return top_level_json;
}

}  // namespace channelz
}  // namespace grpc_core
