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

#include "third_party/json/src/json.hpp"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/atomic.h"
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

using json = nlohmann::json;

namespace grpc_core {
namespace channelz {

//
// channel arg code
//

namespace {

void* parent_uuid_copy(void* p) { return p; }
void parent_uuid_destroy(void* /*p*/) {}
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

BaseNode::BaseNode(EntityType type, std::string name)
    : type_(type), uuid_(-1), name_(std::move(name)) {
  // The registry will set uuid_ under its lock.
  ChannelzRegistry::Register(this);
}

BaseNode::~BaseNode() { ChannelzRegistry::Unregister(uuid_); }

std::string BaseNode::RenderJsonString() {
  json j = RenderJson();
  GPR_ASSERT(!j.is_null());
  return j.dump();
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

void CallCountingHelper::PopulateCallCounts(json* j) {
  CounterData data;
  CollectData(&data);
  if (data.calls_started != 0) {
    (*j)["callsStarted"] = std::to_string(data.calls_started);
  }
  if (data.calls_succeeded != 0) {
    (*j)["callsSucceeded"] = std::to_string(data.calls_succeeded);
  }
  if (data.calls_failed > 0) {
    (*j)["callsFailed"] = std::to_string(data.calls_failed);
  }
  if (data.calls_started != 0) {
    gpr_timespec ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(data.last_call_started_cycle),
        GPR_CLOCK_REALTIME);
    char* tmp = gpr_format_timespec(ts);
    (*j)["lastCallStartedTimestamp"] = tmp;
    free(tmp);
  }
}

//
// ChannelNode
//

ChannelNode::ChannelNode(std::string target, size_t channel_tracer_max_nodes,
                         intptr_t parent_uuid)
    : BaseNode(parent_uuid == 0 ? EntityType::kTopLevelChannel
                                : EntityType::kInternalChannel,
               target),
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

json ChannelNode::RenderJson() {
  json j = {
    {"ref", {{"channelId", std::to_string(uuid())}}},
    {"data", {{"target", target_}}},
  };
  // Connectivity state.  If low-order bit is on, then the field is set.
  int state_field = connectivity_state_.Load(MemoryOrder::RELAXED);
  if ((state_field & 1) != 0) {
    grpc_connectivity_state state =
        static_cast<grpc_connectivity_state>(state_field >> 1);
    j["data"]["state"] = {{"state", ConnectivityStateName(state)}};
  }
  // Fill in the channel trace if applicable.
  json trace_json = trace_.RenderJson();
  if (!trace_json.is_null()) j["data"]["trace"] = std::move(trace_json);
  // Ask CallCountingHelper to populate trace and call count data.
  call_counter_.PopulateCallCounts(&j["data"]);
  // Template method. Child classes may override this to add their specific
  // functionality.
  PopulateChildRefs(&j);
  return j;
}

void ChannelNode::PopulateChildRefs(json* j) {
  MutexLock lock(&child_mu_);
  if (!child_subchannels_.empty()) {
    (*j)["subchannelRef"] = json::array();
    for (const auto& p : child_subchannels_) {
      (*j)["subchannelRef"].push_back(
          {{"subchannelId", std::to_string(p.first)}});
    }
  }
  if (!child_channels_.empty()) {
    (*j)["channelRef"] = json::array();
    for (const auto& p : child_channels_) {
      (*j)["channelRef"].push_back({{"channelId", std::to_string(p.first)}});
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

ServerNode::ServerNode(grpc_server* /*server*/, size_t channel_tracer_max_nodes)
    : BaseNode(EntityType::kServer, ""), trace_(channel_tracer_max_nodes) {}

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

std::string ServerNode::RenderServerSockets(intptr_t start_socket_id,
                                            intptr_t max_results) {
  // If user does not set max_results, we choose 500.
  size_t pagination_limit = max_results == 0 ? 500 : max_results;
  MutexLock lock(&child_mu_);
  json j;
  size_t sockets_rendered = 0;
  if (!child_sockets_.empty()) {
    // Create list of socket refs
    j["socketRef"] = json::array();
    const size_t limit = GPR_MIN(child_sockets_.size(), pagination_limit);
    for (auto it = child_sockets_.lower_bound(start_socket_id);
         it != child_sockets_.end() && sockets_rendered < limit;
         ++it, ++sockets_rendered) {
      j["socketRef"].push_back({
          {"socketId", std::to_string(it->first)},
          {"name", it->second->name()},
      });
    }
  }
  if (sockets_rendered == child_sockets_.size()) j["end"] = true;
  return j.dump();
}

json ServerNode::RenderJson() {
  json j;
  j["ref"] = {{"serverId", std::to_string(uuid())}};
  j["data"] = json::object();
  // Fill in the channel trace if applicable.
  json trace_json = trace_.RenderJson();
  if (!trace_json.is_null()) j["data"]["trace"] = std::move(trace_json);
  // Ask CallCountingHelper to populate trace and call count data.
  call_counter_.PopulateCallCounts(&j["data"]);
  // Render listen sockets.
  MutexLock lock(&child_mu_);
  if (!child_listen_sockets_.empty()) {
    j["listenSocket"] = json::array();
    for (const auto& p : child_listen_sockets_) {
      j["listenSocket"].push_back({
          {"socketId", std::to_string(p.first)},
          {"name", p.second->name()},
      });
    }
  }
  return j;
}

//
// SocketNode
//

namespace {

json CreateSocketAddressJson(const char* addr_str) {
  json j;
  if (addr_str == nullptr) return j;
  grpc_uri* uri = grpc_uri_parse(addr_str, true);
  if ((uri != nullptr) && ((strcmp(uri->scheme, "ipv4") == 0) ||
                           (strcmp(uri->scheme, "ipv6") == 0))) {
    const char* host_port = uri->path;
    if (*host_port == '/') ++host_port;
    grpc_core::UniquePtr<char> host;
    grpc_core::UniquePtr<char> port;
    GPR_ASSERT(SplitHostPort(host_port, &host, &port));
    int port_num = -1;
    if (port != nullptr) {
      port_num = atoi(port.get());
    }
    char* b64_host =
        grpc_base64_encode(host.get(), strlen(host.get()), false, false);
    j["tcpip_address"] = {
        {"port", std::to_string(port_num)},
        {"ip_address", b64_host},
    };
    free(b64_host);
  } else if (uri != nullptr && strcmp(uri->scheme, "unix") == 0) {
    j["uds_address"] = {{"filename", uri->path}};
  } else {
    j["other_address"] = {{"name", addr_str}};
  }
  grpc_uri_destroy(uri);
  return j;
}

}  // namespace

SocketNode::SocketNode(std::string local, std::string remote, std::string name)
    : BaseNode(EntityType::kSocket, std::move(name)),
      local_(std::move(local)),
      remote_(std::move(remote)) {}

void SocketNode::RecordStreamStartedFromLocal() {
  streams_started_.FetchAdd(1, MemoryOrder::RELAXED);
  last_local_stream_created_cycle_.Store(gpr_get_cycle_counter(),
                                         MemoryOrder::RELAXED);
}

void SocketNode::RecordStreamStartedFromRemote() {
  streams_started_.FetchAdd(1, MemoryOrder::RELAXED);
  last_remote_stream_created_cycle_.Store(gpr_get_cycle_counter(),
                                          MemoryOrder::RELAXED);
}

void SocketNode::RecordMessagesSent(uint32_t num_sent) {
  messages_sent_.FetchAdd(num_sent, MemoryOrder::RELAXED);
  last_message_sent_cycle_.Store(gpr_get_cycle_counter(), MemoryOrder::RELAXED);
}

void SocketNode::RecordMessageReceived() {
  messages_received_.FetchAdd(1, MemoryOrder::RELAXED);
  last_message_received_cycle_.Store(gpr_get_cycle_counter(),
                                     MemoryOrder::RELAXED);
}

json SocketNode::RenderJson() {
  json j = {
      {"ref", {
          {"socketId", std::to_string(uuid())},
          {"name", name()},
      }},
  };
  json remote_json = CreateSocketAddressJson(remote_.c_str());
  if (!remote_json.is_null()) {
    j["remote"] = std::move(remote_json);
  }
  json local_json = CreateSocketAddressJson(local_.c_str());
  if (!local_json.is_null()) {
    j["local"] = std::move(local_json);
  }
  // Create and fill the data child.
  j["data"] = json::object();
  gpr_timespec ts;
  int64_t streams_started = streams_started_.Load(MemoryOrder::RELAXED);
  if (streams_started != 0) {
    j["data"]["streamsStarted"] = std::to_string(streams_started);
    gpr_cycle_counter last_local_stream_created_cycle =
        last_local_stream_created_cycle_.Load(MemoryOrder::RELAXED);
    if (last_local_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_local_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      char* tmp = gpr_format_timespec(ts);
      j["data"]["lastLocalStreamCreatedTimestamp"] = tmp;
      free(tmp);
    }
    gpr_cycle_counter last_remote_stream_created_cycle =
        last_remote_stream_created_cycle_.Load(MemoryOrder::RELAXED);
    if (last_remote_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_remote_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      char* tmp = gpr_format_timespec(ts);
      j["data"]["lastRemoteStreamCreatedTimestamp"] = tmp;
      free(tmp);
    }
  }
  int64_t streams_succeeded = streams_succeeded_.Load(MemoryOrder::RELAXED);
  if (streams_succeeded != 0) {
    j["data"]["streamsSucceeded"] = std::to_string(streams_succeeded);
  }
  int64_t streams_failed = streams_failed_.Load(MemoryOrder::RELAXED);
  if (streams_failed) {
    j["data"]["streamsFailed"] = std::to_string(streams_failed);
  }
  int64_t messages_sent = messages_sent_.Load(MemoryOrder::RELAXED);
  if (messages_sent != 0) {
    j["data"]["messagesSent"] = std::to_string(messages_sent);
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            last_message_sent_cycle_.Load(MemoryOrder::RELAXED)),
        GPR_CLOCK_REALTIME);
    char* tmp = gpr_format_timespec(ts);
    j["data"]["lastMessageSentTimestamp"] = tmp;
    free(tmp);
  }
  int64_t messages_received = messages_received_.Load(MemoryOrder::RELAXED);
  if (messages_received != 0) {
    j["data"]["messagesReceived"] = std::to_string(messages_received);
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            last_message_received_cycle_.Load(MemoryOrder::RELAXED)),
        GPR_CLOCK_REALTIME);
    char* tmp = gpr_format_timespec(ts);
    j["data"]["lastMessageReceivedTimestamp"] = tmp;
    free(tmp);
  }
  int64_t keepalives_sent = keepalives_sent_.Load(MemoryOrder::RELAXED);
  if (keepalives_sent != 0) {
    j["data"]["keepAlivesSent"] = std::to_string(keepalives_sent);
  }
  return j;
}

//
// ListenSocketNode
//

ListenSocketNode::ListenSocketNode(std::string local_addr, std::string name)
    : BaseNode(EntityType::kSocket, std::move(name)),
      local_addr_(std::move(local_addr)) {}

json ListenSocketNode::RenderJson() {
  json j = {
      {"ref", {
          {"socketId", std::to_string(uuid())},
          {"name", name()},
      }},
  };
  json local_json = CreateSocketAddressJson(local_addr_.c_str());
  if (!local_json.is_null()) {
    j["local"] = std::move(local_json);
  }
  return j;
}

}  // namespace channelz
}  // namespace grpc_core
