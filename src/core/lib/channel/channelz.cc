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
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
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
  num_cores_ = GPR_MAX(1, gpr_cpu_num_cores());
  per_cpu_counter_data_storage_ = static_cast<AtomicCounterData*>(
      gpr_zalloc(sizeof(AtomicCounterData) * num_cores_));
}

CallCountingHelper::~CallCountingHelper() {
  gpr_free(per_cpu_counter_data_storage_);
}

void CallCountingHelper::RecordCallStarted() {
  gpr_atm_no_barrier_fetch_add(
      &per_cpu_counter_data_storage_[grpc_core::ExecCtx::Get()->starting_cpu()]
           .calls_started,
      static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(
      &per_cpu_counter_data_storage_[grpc_core::ExecCtx::Get()->starting_cpu()]
           .last_call_started_millis,
      (gpr_atm)ExecCtx::Get()->Now());
}

void CallCountingHelper::RecordCallFailed() {
  gpr_atm_no_barrier_fetch_add(
      &per_cpu_counter_data_storage_[grpc_core::ExecCtx::Get()->starting_cpu()]
           .calls_failed,
      static_cast<gpr_atm>(1));
}

void CallCountingHelper::RecordCallSucceeded() {
  gpr_atm_no_barrier_fetch_add(
      &per_cpu_counter_data_storage_[grpc_core::ExecCtx::Get()->starting_cpu()]
           .calls_succeeded,
      static_cast<gpr_atm>(1));
}

void CallCountingHelper::CollectData(CounterData* out) {
  for (size_t core = 0; core < num_cores_; ++core) {
    out->calls_started += gpr_atm_no_barrier_load(
        &per_cpu_counter_data_storage_[core].calls_started);
    out->calls_succeeded += gpr_atm_no_barrier_load(
        &per_cpu_counter_data_storage_[core].calls_succeeded);
    out->calls_failed += gpr_atm_no_barrier_load(
        &per_cpu_counter_data_storage_[core].calls_failed);
    gpr_atm last_call = gpr_atm_no_barrier_load(
        &per_cpu_counter_data_storage_[core].last_call_started_millis);
    if (last_call > out->last_call_started_millis) {
      out->last_call_started_millis = last_call;
    }
  }
}

void CallCountingHelper::PopulateCallCounts(grpc_json* json) {
  grpc_json* json_iterator = nullptr;
  CounterData data;
  CollectData(&data);
  if (data.calls_started != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "callsStarted", data.calls_started);
  }
  if (data.calls_succeeded != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "callsSucceeded", data.calls_succeeded);
  }
  if (data.calls_failed) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "callsFailed", data.calls_failed);
  }
  if (data.calls_started != 0) {
    gpr_timespec ts = grpc_millis_to_timespec(data.last_call_started_millis,
                                              GPR_CLOCK_REALTIME);
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

ServerNode::ServerNode(size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kServer), trace_(channel_tracer_max_nodes) {}

ServerNode::~ServerNode() {}

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
