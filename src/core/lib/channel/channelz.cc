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
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
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

BaseNode::BaseNode(EntityType type) : type_(type), uuid_(-1) {
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

//
// ChannelNode
//

ChannelNode::ChannelNode(UniquePtr<char> target,
                         size_t channel_tracer_max_nodes, intptr_t parent_uuid)
    : BaseNode(parent_uuid == 0 ? EntityType::kTopLevelChannel
                                : EntityType::kInternalChannel),
      target_(std::move(target)),
      trace_(channel_tracer_max_nodes),
      parent_uuid_(parent_uuid) {}

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
    grpc_json_create_child(nullptr, json, "state",
                           grpc_connectivity_state_name(state),
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
  child_channels_.insert(MakePair(child_uuid, true));
}

void ChannelNode::RemoveChildChannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_channels_.erase(child_uuid);
}

void ChannelNode::AddChildSubchannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_subchannels_.insert(MakePair(child_uuid, true));
}

void ChannelNode::RemoveChildSubchannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_subchannels_.erase(child_uuid);
}

//
// ServerNode
//

ServerNode::ServerNode(grpc_server* server, size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kServer),
      server_(server),
      trace_(channel_tracer_max_nodes) {}

ServerNode::~ServerNode() {}

char* ServerNode::RenderServerSockets(intptr_t start_socket_id,
                                      intptr_t max_results) {
  // if user does not set max_results, we choose 500.
  size_t pagination_limit = max_results == 0 ? 500 : max_results;
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  ChildSocketsList socket_refs;
  grpc_server_populate_server_sockets(server_, &socket_refs, start_socket_id);
  // declared early so it can be used outside of the loop.
  size_t i = 0;
  if (!socket_refs.empty()) {
    // create list of socket refs
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "socketRef", nullptr, GRPC_JSON_ARRAY, false);
    for (i = 0; i < GPR_MIN(socket_refs.size(), pagination_limit); ++i) {
      grpc_json* socket_ref_json = grpc_json_create_child(
          nullptr, array_parent, nullptr, nullptr, GRPC_JSON_OBJECT, false);
      json_iterator = grpc_json_add_number_string_child(
          socket_ref_json, nullptr, "socketId", socket_refs[i]->uuid());
      grpc_json_create_child(json_iterator, socket_ref_json, "name",
                             socket_refs[i]->remote(), GRPC_JSON_STRING, false);
    }
  }
  if (i == socket_refs.size()) {
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
  ChildRefsList listen_sockets;
  grpc_server_populate_listen_sockets(server_, &listen_sockets);
  if (!listen_sockets.empty()) {
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "listenSocket", nullptr, GRPC_JSON_ARRAY, false);
    for (size_t i = 0; i < listen_sockets.size(); ++i) {
      json_iterator =
          grpc_json_create_child(json_iterator, array_parent, nullptr, nullptr,
                                 GRPC_JSON_OBJECT, false);
      grpc_json_add_number_string_child(json_iterator, nullptr, "socketId",
                                        listen_sockets[i]);
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
    char* host = nullptr;
    char* port = nullptr;
    GPR_ASSERT(gpr_split_host_port(host_port, &host, &port));
    int port_num = -1;
    if (port != nullptr) {
      port_num = atoi(port);
    }
    char* b64_host = grpc_base64_encode(host, strlen(host), false, false);
    json_iterator = grpc_json_create_child(json_iterator, json, "tcpip_address",
                                           nullptr, GRPC_JSON_OBJECT, false);
    json = json_iterator;
    json_iterator = nullptr;
    json_iterator = grpc_json_add_number_string_child(json, json_iterator,
                                                      "port", port_num);
    json_iterator = grpc_json_create_child(json_iterator, json, "ip_address",
                                           b64_host, GRPC_JSON_STRING, true);
    gpr_free(host);
    gpr_free(port);
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

SocketNode::SocketNode(UniquePtr<char> local, UniquePtr<char> remote)
    : BaseNode(EntityType::kSocket),
      local_(std::move(local)),
      remote_(std::move(remote)) {}

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
    gpr_atm last_local_stream_created_millis =
        gpr_atm_no_barrier_load(&last_local_stream_created_millis_);
    if (last_local_stream_created_millis != 0) {
      ts = grpc_millis_to_timespec(last_local_stream_created_millis,
                                   GPR_CLOCK_REALTIME);
      json_iterator = grpc_json_create_child(
          json_iterator, json, "lastLocalStreamCreatedTimestamp",
          gpr_format_timespec(ts), GRPC_JSON_STRING, true);
    }
    gpr_atm last_remote_stream_created_millis =
        gpr_atm_no_barrier_load(&last_remote_stream_created_millis_);
    if (last_remote_stream_created_millis != 0) {
      ts = grpc_millis_to_timespec(last_remote_stream_created_millis,
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
    ts = grpc_millis_to_timespec(
        gpr_atm_no_barrier_load(&last_message_sent_millis_),
        GPR_CLOCK_REALTIME);
    json_iterator =
        grpc_json_create_child(json_iterator, json, "lastMessageSentTimestamp",
                               gpr_format_timespec(ts), GRPC_JSON_STRING, true);
  }
  gpr_atm messages_received = gpr_atm_no_barrier_load(&messages_received_);
  if (messages_received != 0) {
    json_iterator = grpc_json_add_number_string_child(
        json, json_iterator, "messagesReceived", messages_received);
    ts = grpc_millis_to_timespec(
        gpr_atm_no_barrier_load(&last_message_received_millis_),
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

ListenSocketNode::ListenSocketNode(UniquePtr<char> local_addr)
    : BaseNode(EntityType::kSocket), local_addr_(std::move(local_addr)) {}

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
  json = top_level_json;
  PopulateSocketAddressJson(json, "local", local_addr_.get());

  return top_level_json;
}

}  // namespace channelz
}  // namespace grpc_core
