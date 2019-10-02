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
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace channelz {

//
// channel arg code
//

namespace {

void* parent_uuid_copy(void* p) { return p; }
void parent_uuid_destroy(void* p) {}
int parent_uuid_cmp(void* p1, void* p2) { return GPR_ICMP(p1, p2); }
const grpc_arg_pointer_vtable parent_uuid_vtable = {
    parent_uuid_copy, parent_uuid_destroy, parent_uuid_cmp};

}  // namespace

grpc_arg MakeParentUuidArg(intptr_t parent_uuid) {
  // We would ideally like to store the uuid in an integer argument.
  // Unfortunately, that won't work, because intptr_t (the type used for
  // uuids) doesn't fit in an int (the type used for integer args).
  // So instead, we use a hack to store it as a pointer, because
  // intptr_t should be the same size as void*.
  static_assert(sizeof(intptr_t) <= sizeof(void*),
                "can't fit intptr_t inside of void*");
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CHANNELZ_PARENT_UUID),
      reinterpret_cast<void*>(parent_uuid), &parent_uuid_vtable);
}

intptr_t GetParentUuidFromArgs(const grpc_channel_args& args) {
  const grpc_arg* arg =
      grpc_channel_args_find(&args, GRPC_ARG_CHANNELZ_PARENT_UUID);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) return 0;
  return reinterpret_cast<intptr_t>(arg->value.pointer.p);
}

//
// BaseNode
//

BaseNode::BaseNode(EntityType type, UniquePtr<char> name)
    : type_(type), uuid_(-1), name_(std::move(name)) {
  // The registry will set uuid_ under its lock.
  ChannelzRegistry::Register(this);
}

BaseNode::~BaseNode() { ChannelzRegistry::Unregister(uuid_); }

char* BaseNode::RenderJsonString() {
  grpc_json* json = RenderJson();
  GPR_ASSERT(json != nullptr);
  char* json_str = grpc_json_dump_to_string(json, 0);
  grpc_json_destroy(json);
  return json_str;
}

//
// CallCountingHelper
//

CallCountingHelper::CallCountingHelper() {
  num_cores_ = GPR_MAX(1, gpr_cpu_num_cores());
  per_cpu_counter_data_storage_.reserve(num_cores_);
  for (size_t i = 0; i < num_cores_; ++i) {
    per_cpu_counter_data_storage_.emplace_back();
  }
}

void CallCountingHelper::RecordCallStarted() {
  AtomicCounterData& data =
      per_cpu_counter_data_storage_[ExecCtx::Get()->starting_cpu()];
  data.calls_started.FetchAdd(1, MemoryOrder::RELAXED);
  data.last_call_started_cycle.Store(gpr_get_cycle_counter(),
                                     MemoryOrder::RELAXED);
}

void CallCountingHelper::RecordCallFailed() {
  per_cpu_counter_data_storage_[ExecCtx::Get()->starting_cpu()]
      .calls_failed.FetchAdd(1, MemoryOrder::RELAXED);
}

void CallCountingHelper::RecordCallSucceeded() {
  per_cpu_counter_data_storage_[ExecCtx::Get()->starting_cpu()]
      .calls_succeeded.FetchAdd(1, MemoryOrder::RELAXED);
}

void CallCountingHelper::CollectData(CounterData* out) {
  for (size_t core = 0; core < num_cores_; ++core) {
    AtomicCounterData& data = per_cpu_counter_data_storage_[core];

    out->calls_started += data.calls_started.Load(MemoryOrder::RELAXED);
    out->calls_succeeded +=
        per_cpu_counter_data_storage_[core].calls_succeeded.Load(
            MemoryOrder::RELAXED);
    out->calls_failed += per_cpu_counter_data_storage_[core].calls_failed.Load(
        MemoryOrder::RELAXED);
    const gpr_cycle_counter last_call =
        per_cpu_counter_data_storage_[core].last_call_started_cycle.Load(
            MemoryOrder::RELAXED);
    if (last_call > out->last_call_started_cycle) {
      out->last_call_started_cycle = last_call;
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
    gpr_timespec ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(data.last_call_started_cycle),
        GPR_CLOCK_REALTIME);
    json_iterator =
        grpc_json_create_child(json_iterator, json, "lastCallStartedTimestamp",
                               gpr_format_timespec(ts), GRPC_JSON_STRING, true);
  }
}

//
// ChannelNode
//

ChannelNode::ChannelNode(UniquePtr<char> target,
                         size_t channel_tracer_max_nodes, intptr_t parent_uuid)
    : BaseNode(parent_uuid == 0 ? EntityType::kTopLevelChannel
                                : EntityType::kInternalChannel,
               UniquePtr<char>(gpr_strdup(target.get()))),
      target_(std::move(target)),
      trace_(channel_tracer_max_nodes),
      parent_uuid_(parent_uuid) {}

const char* ChannelNode::GetChannelConnectivityStateChangeString(
    grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE:
      return "Channel state change to IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "Channel state change to CONNECTING";
    case GRPC_CHANNEL_READY:
      return "Channel state change to READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "Channel state change to TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "Channel state change to SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

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
  // connectivity state
  // If low-order bit is on, then the field is set.
  int state_field = connectivity_state_.Load(MemoryOrder::RELAXED);
  if ((state_field & 1) != 0) {
    grpc_connectivity_state state =
        static_cast<grpc_connectivity_state>(state_field >> 1);
    json = grpc_json_create_child(nullptr, json, "state", nullptr,
                                  GRPC_JSON_OBJECT, false);
    grpc_json_create_child(nullptr, json, "state", ConnectivityStateName(state),
                           GRPC_JSON_STRING, false);
    json = data;
  }
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

void ChannelNode::PopulateChildRefs(grpc_json* json) {
  MutexLock lock(&child_mu_);
  grpc_json* json_iterator = nullptr;
  if (!child_subchannels_.empty()) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "subchannelRef", nullptr, GRPC_JSON_ARRAY, false);
    for (const auto& p : child_subchannels_) {
      json_iterator =
          grpc_json_create_child(json_iterator, array_parent, nullptr, nullptr,
                                 GRPC_JSON_OBJECT, false);
      grpc_json_add_number_string_child(json_iterator, nullptr, "subchannelId",
                                        p.first);
    }
  }
  if (!child_channels_.empty()) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "channelRef", nullptr, GRPC_JSON_ARRAY, false);
    json_iterator = nullptr;
    for (const auto& p : child_channels_) {
      json_iterator =
          grpc_json_create_child(json_iterator, array_parent, nullptr, nullptr,
                                 GRPC_JSON_OBJECT, false);
      grpc_json_add_number_string_child(json_iterator, nullptr, "channelId",
                                        p.first);
    }
  }
}

void ChannelNode::SetConnectivityState(grpc_connectivity_state state) {
  // Store with low-order bit set to indicate that the field is set.
  int state_field = (state << 1) + 1;
  connectivity_state_.Store(state_field, MemoryOrder::RELAXED);
}

void ChannelNode::AddChildChannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_channels_.insert(std::make_pair(child_uuid, true));
}

void ChannelNode::RemoveChildChannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_channels_.erase(child_uuid);
}

void ChannelNode::AddChildSubchannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_subchannels_.insert(std::make_pair(child_uuid, true));
}

void ChannelNode::RemoveChildSubchannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_subchannels_.erase(child_uuid);
}

//
// ServerNode
//

ServerNode::ServerNode(grpc_server* server, size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kServer, /* name */ nullptr),
      trace_(channel_tracer_max_nodes) {}

ServerNode::~ServerNode() {}

void ServerNode::AddChildSocket(RefCountedPtr<SocketNode> node) {
  MutexLock lock(&child_mu_);
  child_sockets_.insert(std::make_pair(node->uuid(), std::move(node)));
}

void ServerNode::RemoveChildSocket(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_sockets_.erase(child_uuid);
}

void ServerNode::AddChildListenSocket(RefCountedPtr<ListenSocketNode> node) {
  MutexLock lock(&child_mu_);
  child_listen_sockets_.insert(std::make_pair(node->uuid(), std::move(node)));
}

void ServerNode::RemoveChildListenSocket(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_listen_sockets_.erase(child_uuid);
}

char* ServerNode::RenderServerSockets(intptr_t start_socket_id,
                                      intptr_t max_results) {
  // If user does not set max_results, we choose 500.
  size_t pagination_limit = max_results == 0 ? 500 : max_results;
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  MutexLock lock(&child_mu_);
  size_t sockets_rendered = 0;
  if (!child_sockets_.empty()) {
    // Create list of socket refs
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "socketRef", nullptr, GRPC_JSON_ARRAY, false);
    const size_t limit = GPR_MIN(child_sockets_.size(), pagination_limit);
    for (auto it = child_sockets_.lower_bound(start_socket_id);
         it != child_sockets_.end() && sockets_rendered < limit;
         ++it, ++sockets_rendered) {
      grpc_json* socket_ref_json = grpc_json_create_child(
          nullptr, array_parent, nullptr, nullptr, GRPC_JSON_OBJECT, false);
      json_iterator = grpc_json_add_number_string_child(
          socket_ref_json, nullptr, "socketId", it->first);
      grpc_json_create_child(json_iterator, socket_ref_json, "name",
                             it->second->name(), GRPC_JSON_STRING, false);
    }
  }
  if (sockets_rendered == child_sockets_.size()) {
    json_iterator = grpc_json_create_child(nullptr, json, "end", nullptr,
                                           GRPC_JSON_TRUE, false);
  }
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
  json = top_level_json;
  // Render listen sockets
  MutexLock lock(&child_mu_);
  if (!child_listen_sockets_.empty()) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "listenSocket", nullptr, GRPC_JSON_ARRAY, false);
    for (const auto& it : child_listen_sockets_) {
      json_iterator =
          grpc_json_create_child(json_iterator, array_parent, nullptr, nullptr,
                                 GRPC_JSON_OBJECT, false);
      grpc_json* sibling_iterator = grpc_json_add_number_string_child(
          json_iterator, nullptr, "socketId", it.first);
      grpc_json_create_child(sibling_iterator, json_iterator, "name",
                             it.second->name(), GRPC_JSON_STRING, false);
    }
  }
  return top_level_json;
}

//
// SocketNode
//

namespace {

void PopulateSocketAddressJson(grpc_json* json, const char* name,
                               const char* addr_str) {
  if (addr_str == nullptr) return;
  grpc_json* json_iterator = nullptr;
  json_iterator = grpc_json_create_child(json_iterator, json, name, nullptr,
                                         GRPC_JSON_OBJECT, false);
  json = json_iterator;
  json_iterator = nullptr;
  grpc_uri* uri = grpc_uri_parse(addr_str, true);
  if ((uri != nullptr) && ((strcmp(uri->scheme, "ipv4") == 0) ||
                           (strcmp(uri->scheme, "ipv6") == 0))) {
    const char* host_port = uri->path;
    if (*host_port == '/') ++host_port;
    UniquePtr<char> host;
    UniquePtr<char> port;
    GPR_ASSERT(SplitHostPort(host_port, &host, &port));
    int port_num = -1;
    if (port != nullptr) {
      port_num = atoi(port.get());
    }
    char* b64_host =
        grpc_base64_encode(host.get(), strlen(host.get()), false, false);
    json_iterator = grpc_json_create_child(json_iterator, json, "tcpip_address",
                                           nullptr, GRPC_JSON_OBJECT, false);
    json = json_iterator;
    json_iterator = nullptr;
    json_iterator = grpc_json_add_number_string_child(json, json_iterator,
                                                      "port", port_num);
    json_iterator = grpc_json_create_child(json_iterator, json, "ip_address",
                                           b64_host, GRPC_JSON_STRING, true);
  } else if (uri != nullptr && strcmp(uri->scheme, "unix") == 0) {
    json_iterator = grpc_json_create_child(json_iterator, json, "uds_address",
                                           nullptr, GRPC_JSON_OBJECT, false);
    json = json_iterator;
    json_iterator = nullptr;
    json_iterator =
        grpc_json_create_child(json_iterator, json, "filename",
                               gpr_strdup(uri->path), GRPC_JSON_STRING, true);
  } else {
    json_iterator = grpc_json_create_child(json_iterator, json, "other_address",
                                           nullptr, GRPC_JSON_OBJECT, false);
    json = json_iterator;
    json_iterator = nullptr;
    json_iterator = grpc_json_create_child(json_iterator, json, "name",
                                           addr_str, GRPC_JSON_STRING, false);
  }
  grpc_uri_destroy(uri);
}

}  // namespace

SocketNode::SocketNode(UniquePtr<char> local, UniquePtr<char> remote,
                       UniquePtr<char> name)
    : BaseNode(EntityType::kSocket, std::move(name)),
      local_(std::move(local)),
      remote_(std::move(remote)) {}

void SocketNode::RecordStreamStartedFromLocal() {
  gpr_atm_no_barrier_fetch_add(&streams_started_, static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(&last_local_stream_created_cycle_,
                           gpr_get_cycle_counter());
}

void SocketNode::RecordStreamStartedFromRemote() {
  gpr_atm_no_barrier_fetch_add(&streams_started_, static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(&last_remote_stream_created_cycle_,
                           gpr_get_cycle_counter());
}

void SocketNode::RecordMessagesSent(uint32_t num_sent) {
  gpr_atm_no_barrier_fetch_add(&messages_sent_, static_cast<gpr_atm>(num_sent));
  gpr_atm_no_barrier_store(&last_message_sent_cycle_, gpr_get_cycle_counter());
}

void SocketNode::RecordMessageReceived() {
  gpr_atm_no_barrier_fetch_add(&messages_received_, static_cast<gpr_atm>(1));
  gpr_atm_no_barrier_store(&last_message_received_cycle_,
                           gpr_get_cycle_counter());
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
  json_iterator = grpc_json_create_child(json_iterator, json, "name", name(),
                                         GRPC_JSON_STRING, false);
  json = top_level_json;
  PopulateSocketAddressJson(json, "remote", remote_.get());
  PopulateSocketAddressJson(json, "local", local_.get());
  // reset json iterators to top level object
  json = top_level_json;
  json_iterator = nullptr;
  // create and fill the data child.
  grpc_json* data = grpc_json_create_child(json_iterator, json, "data", nullptr,
                                           GRPC_JSON_OBJECT, false);
  json = data;
  json_iterator = nullptr;
  gpr_timespec ts;
  gpr_atm streams_started = gpr_atm_no_barrier_load(&streams_started_);
  if (streams_started != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "streamsStarted", streams_started);
    gpr_cycle_counter last_local_stream_created_cycle =
        gpr_atm_no_barrier_load(&last_local_stream_created_cycle_);
    if (last_local_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_local_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      json_iterator = grpc_json_create_child(
          json_iterator, json, "lastLocalStreamCreatedTimestamp",
          gpr_format_timespec(ts), GRPC_JSON_STRING, true);
    }
    gpr_cycle_counter last_remote_stream_created_cycle =
        gpr_atm_no_barrier_load(&last_remote_stream_created_cycle_);
    if (last_remote_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_remote_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      json_iterator = grpc_json_create_child(
          json_iterator, json, "lastRemoteStreamCreatedTimestamp",
          gpr_format_timespec(ts), GRPC_JSON_STRING, true);
    }
  }
  gpr_atm streams_succeeded = gpr_atm_no_barrier_load(&streams_succeeded_);
  if (streams_succeeded != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "streamsSucceeded", streams_succeeded);
  }
  gpr_atm streams_failed = gpr_atm_no_barrier_load(&streams_failed_);
  if (streams_failed) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "streamsFailed", streams_failed);
  }
  gpr_atm messages_sent = gpr_atm_no_barrier_load(&messages_sent_);
  if (messages_sent != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "messagesSent", messages_sent);
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            gpr_atm_no_barrier_load(&last_message_sent_cycle_)),
        GPR_CLOCK_REALTIME);
    json_iterator =
        grpc_json_create_child(json_iterator, json, "lastMessageSentTimestamp",
                               gpr_format_timespec(ts), GRPC_JSON_STRING, true);
  }
  gpr_atm messages_received = gpr_atm_no_barrier_load(&messages_received_);
  if (messages_received != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "messagesReceived", messages_received);
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            gpr_atm_no_barrier_load(&last_message_received_cycle_)),
        GPR_CLOCK_REALTIME);
    json_iterator = grpc_json_create_child(
        json_iterator, json, "lastMessageReceivedTimestamp",
        gpr_format_timespec(ts), GRPC_JSON_STRING, true);
  }
  gpr_atm keepalives_sent = gpr_atm_no_barrier_load(&keepalives_sent_);
  if (keepalives_sent != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "keepAlivesSent", keepalives_sent);
  }
  return top_level_json;
}

//
// ListenSocketNode
//

ListenSocketNode::ListenSocketNode(UniquePtr<char> local_addr,
                                   UniquePtr<char> name)
    : BaseNode(EntityType::kSocket, std::move(name)),
      local_addr_(std::move(local_addr)) {}

grpc_json* ListenSocketNode::RenderJson() {
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
  json_iterator = grpc_json_create_child(json_iterator, json, "name", name(),
                                         GRPC_JSON_STRING, false);
  json = top_level_json;
  PopulateSocketAddressJson(json, "local", local_addr_.get());

  return top_level_json;
}

}  // namespace channelz
}  // namespace grpc_core
