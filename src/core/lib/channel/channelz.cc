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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channelz.h"

#include <algorithm>
#include <atomic>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/strip.h"

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace channelz {

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
  Json json = RenderJson();
  return json.Dump();
}

//
// CallCountingHelper
//

CallCountingHelper::CallCountingHelper() {
  num_cores_ = std::max(1u, gpr_cpu_num_cores());
  per_cpu_counter_data_storage_.reserve(num_cores_);
  for (size_t i = 0; i < num_cores_; ++i) {
    per_cpu_counter_data_storage_.emplace_back();
  }
}

void CallCountingHelper::RecordCallStarted() {
  AtomicCounterData& data =
      per_cpu_counter_data_storage_[ExecCtx::Get()->starting_cpu()];
  data.calls_started.fetch_add(1, std::memory_order_relaxed);
  data.last_call_started_cycle.store(gpr_get_cycle_counter(),
                                     std::memory_order_relaxed);
}

void CallCountingHelper::RecordCallFailed() {
  per_cpu_counter_data_storage_[ExecCtx::Get()->starting_cpu()]
      .calls_failed.fetch_add(1, std::memory_order_relaxed);
}

void CallCountingHelper::RecordCallSucceeded() {
  per_cpu_counter_data_storage_[ExecCtx::Get()->starting_cpu()]
      .calls_succeeded.fetch_add(1, std::memory_order_relaxed);
}

void CallCountingHelper::CollectData(CounterData* out) {
  for (size_t core = 0; core < num_cores_; ++core) {
    AtomicCounterData& data = per_cpu_counter_data_storage_[core];

    out->calls_started += data.calls_started.load(std::memory_order_relaxed);
    out->calls_succeeded +=
        per_cpu_counter_data_storage_[core].calls_succeeded.load(
            std::memory_order_relaxed);
    out->calls_failed += per_cpu_counter_data_storage_[core].calls_failed.load(
        std::memory_order_relaxed);
    const gpr_cycle_counter last_call =
        per_cpu_counter_data_storage_[core].last_call_started_cycle.load(
            std::memory_order_relaxed);
    if (last_call > out->last_call_started_cycle) {
      out->last_call_started_cycle = last_call;
    }
  }
}

void CallCountingHelper::PopulateCallCounts(Json::Object* json) {
  CounterData data;
  CollectData(&data);
  if (data.calls_started != 0) {
    (*json)["callsStarted"] = std::to_string(data.calls_started);
    gpr_timespec ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(data.last_call_started_cycle),
        GPR_CLOCK_REALTIME);
    (*json)["lastCallStartedTimestamp"] = gpr_format_timespec(ts);
  }
  if (data.calls_succeeded != 0) {
    (*json)["callsSucceeded"] = std::to_string(data.calls_succeeded);
  }
  if (data.calls_failed) {
    (*json)["callsFailed"] = std::to_string(data.calls_failed);
  }
}

//
// ChannelNode
//

ChannelNode::ChannelNode(std::string target, size_t channel_tracer_max_nodes,
                         bool is_internal_channel)
    : BaseNode(is_internal_channel ? EntityType::kInternalChannel
                                   : EntityType::kTopLevelChannel,
               target),
      target_(std::move(target)),
      trace_(channel_tracer_max_nodes) {}

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

Json ChannelNode::RenderJson() {
  Json::Object data = {
      {"target", target_},
  };
  // Connectivity state.
  // If low-order bit is on, then the field is set.
  int state_field = connectivity_state_.load(std::memory_order_relaxed);
  if ((state_field & 1) != 0) {
    grpc_connectivity_state state =
        static_cast<grpc_connectivity_state>(state_field >> 1);
    data["state"] = Json::Object{
        {"state", ConnectivityStateName(state)},
    };
  }
  // Fill in the channel trace if applicable.
  Json trace_json = trace_.RenderJson();
  if (trace_json.type() != Json::Type::JSON_NULL) {
    data["trace"] = std::move(trace_json);
  }
  // Ask CallCountingHelper to populate call count data.
  call_counter_.PopulateCallCounts(&data);
  // Construct outer object.
  Json::Object json = {
      {"ref",
       Json::Object{
           {"channelId", std::to_string(uuid())},
       }},
      {"data", std::move(data)},
  };
  // Template method. Child classes may override this to add their specific
  // functionality.
  PopulateChildRefs(&json);
  return json;
}

void ChannelNode::PopulateChildRefs(Json::Object* json) {
  MutexLock lock(&child_mu_);
  if (!child_subchannels_.empty()) {
    Json::Array array;
    for (intptr_t subchannel_uuid : child_subchannels_) {
      array.emplace_back(Json::Object{
          {"subchannelId", std::to_string(subchannel_uuid)},
      });
    }
    (*json)["subchannelRef"] = std::move(array);
  }
  if (!child_channels_.empty()) {
    Json::Array array;
    for (intptr_t channel_uuid : child_channels_) {
      array.emplace_back(Json::Object{
          {"channelId", std::to_string(channel_uuid)},
      });
    }
    (*json)["channelRef"] = std::move(array);
  }
}

void ChannelNode::SetConnectivityState(grpc_connectivity_state state) {
  // Store with low-order bit set to indicate that the field is set.
  int state_field = (state << 1) + 1;
  connectivity_state_.store(state_field, std::memory_order_relaxed);
}

void ChannelNode::AddChildChannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_channels_.insert(child_uuid);
}

void ChannelNode::RemoveChildChannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_channels_.erase(child_uuid);
}

void ChannelNode::AddChildSubchannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_subchannels_.insert(child_uuid);
}

void ChannelNode::RemoveChildSubchannel(intptr_t child_uuid) {
  MutexLock lock(&child_mu_);
  child_subchannels_.erase(child_uuid);
}

//
// ServerNode
//

ServerNode::ServerNode(size_t channel_tracer_max_nodes)
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
  GPR_ASSERT(start_socket_id >= 0);
  GPR_ASSERT(max_results >= 0);
  // If user does not set max_results, we choose 500.
  size_t pagination_limit = max_results == 0 ? 500 : max_results;
  Json::Object object;
  {
    MutexLock lock(&child_mu_);
    size_t sockets_rendered = 0;
    // Create list of socket refs.
    Json::Array array;
    auto it = child_sockets_.lower_bound(start_socket_id);
    for (; it != child_sockets_.end() && sockets_rendered < pagination_limit;
         ++it, ++sockets_rendered) {
      array.emplace_back(Json::Object{
          {"socketId", std::to_string(it->first)},
          {"name", it->second->name()},
      });
    }
    object["socketRef"] = std::move(array);
    if (it == child_sockets_.end()) object["end"] = true;
  }
  Json json = std::move(object);
  return json.Dump();
}

Json ServerNode::RenderJson() {
  Json::Object data;
  // Fill in the channel trace if applicable.
  Json trace_json = trace_.RenderJson();
  if (trace_json.type() != Json::Type::JSON_NULL) {
    data["trace"] = std::move(trace_json);
  }
  // Ask CallCountingHelper to populate call count data.
  call_counter_.PopulateCallCounts(&data);
  // Construct top-level object.
  Json::Object object = {
      {"ref",
       Json::Object{
           {"serverId", std::to_string(uuid())},
       }},
      {"data", std::move(data)},
  };
  // Render listen sockets.
  {
    MutexLock lock(&child_mu_);
    if (!child_listen_sockets_.empty()) {
      Json::Array array;
      for (const auto& it : child_listen_sockets_) {
        array.emplace_back(Json::Object{
            {"socketId", std::to_string(it.first)},
            {"name", it.second->name()},
        });
      }
      object["listenSocket"] = std::move(array);
    }
  }
  return object;
}

//
// SocketNode::Security::Tls
//

Json SocketNode::Security::Tls::RenderJson() {
  Json::Object data;
  if (type == NameType::kStandardName) {
    data["standard_name"] = name;
  } else if (type == NameType::kOtherName) {
    data["other_name"] = name;
  }
  if (!local_certificate.empty()) {
    data["local_certificate"] = absl::Base64Escape(local_certificate);
  }
  if (!remote_certificate.empty()) {
    data["remote_certificate"] = absl::Base64Escape(remote_certificate);
  }
  return data;
}

//
// SocketNode::Security
//

Json SocketNode::Security::RenderJson() {
  Json::Object data;
  switch (type) {
    case ModelType::kUnset:
      break;
    case ModelType::kTls:
      if (tls) {
        data["tls"] = tls->RenderJson();
      }
      break;
    case ModelType::kOther:
      if (other) {
        data["other"] = *other;
      }
      break;
  }
  return data;
}

namespace {

void* SecurityArgCopy(void* p) {
  SocketNode::Security* xds_certificate_provider =
      static_cast<SocketNode::Security*>(p);
  return xds_certificate_provider->Ref().release();
}

void SecurityArgDestroy(void* p) {
  SocketNode::Security* xds_certificate_provider =
      static_cast<SocketNode::Security*>(p);
  xds_certificate_provider->Unref();
}

int SecurityArgCmp(void* p, void* q) { return QsortCompare(p, q); }

const grpc_arg_pointer_vtable kChannelArgVtable = {
    SecurityArgCopy, SecurityArgDestroy, SecurityArgCmp};

}  // namespace

grpc_arg SocketNode::Security::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CHANNELZ_SECURITY),
      const_cast<SocketNode::Security*>(this), &kChannelArgVtable);
}

RefCountedPtr<SocketNode::Security> SocketNode::Security::GetFromChannelArgs(
    const grpc_channel_args* args) {
  Security* security = grpc_channel_args_find_pointer<Security>(
      args, GRPC_ARG_CHANNELZ_SECURITY);
  return security != nullptr ? security->Ref() : nullptr;
}

//
// SocketNode
//

namespace {

void PopulateSocketAddressJson(Json::Object* json, const char* name,
                               const char* addr_str) {
  if (addr_str == nullptr) return;
  absl::StatusOr<URI> uri = URI::Parse(addr_str);
  if (uri.ok()) {
    if (uri->scheme() == "ipv4" || uri->scheme() == "ipv6") {
      auto address = StringToSockaddr(absl::StripPrefix(uri->path(), "/"));
      if (address.ok()) {
        std::string packed_host = grpc_sockaddr_get_packed_host(&*address);
        (*json)[name] = Json::Object{
            {"tcpip_address",
             Json::Object{
                 {"port", grpc_sockaddr_get_port(&*address)},
                 {"ip_address", absl::Base64Escape(packed_host)},
             }},
        };
        return;
      }
    } else if (uri->scheme() == "unix") {
      (*json)[name] = Json::Object{
          {"uds_address",
           Json::Object{
               {"filename", uri->path()},
           }},
      };
      return;
    }
  }
  // Unknown address type.
  (*json)[name] = Json::Object{
      {"other_address",
       Json::Object{
           {"name", addr_str},
       }},
  };
}

}  // namespace

SocketNode::SocketNode(std::string local, std::string remote, std::string name,
                       RefCountedPtr<Security> security)
    : BaseNode(EntityType::kSocket, std::move(name)),
      local_(std::move(local)),
      remote_(std::move(remote)),
      security_(std::move(security)) {}

void SocketNode::RecordStreamStartedFromLocal() {
  streams_started_.fetch_add(1, std::memory_order_relaxed);
  last_local_stream_created_cycle_.store(gpr_get_cycle_counter(),
                                         std::memory_order_relaxed);
}

void SocketNode::RecordStreamStartedFromRemote() {
  streams_started_.fetch_add(1, std::memory_order_relaxed);
  last_remote_stream_created_cycle_.store(gpr_get_cycle_counter(),
                                          std::memory_order_relaxed);
}

void SocketNode::RecordMessagesSent(uint32_t num_sent) {
  messages_sent_.fetch_add(num_sent, std::memory_order_relaxed);
  last_message_sent_cycle_.store(gpr_get_cycle_counter(),
                                 std::memory_order_relaxed);
}

void SocketNode::RecordMessageReceived() {
  messages_received_.fetch_add(1, std::memory_order_relaxed);
  last_message_received_cycle_.store(gpr_get_cycle_counter(),
                                     std::memory_order_relaxed);
}

Json SocketNode::RenderJson() {
  // Create and fill the data child.
  Json::Object data;
  gpr_timespec ts;
  int64_t streams_started = streams_started_.load(std::memory_order_relaxed);
  if (streams_started != 0) {
    data["streamsStarted"] = std::to_string(streams_started);
    gpr_cycle_counter last_local_stream_created_cycle =
        last_local_stream_created_cycle_.load(std::memory_order_relaxed);
    if (last_local_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_local_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      data["lastLocalStreamCreatedTimestamp"] = gpr_format_timespec(ts);
    }
    gpr_cycle_counter last_remote_stream_created_cycle =
        last_remote_stream_created_cycle_.load(std::memory_order_relaxed);
    if (last_remote_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_remote_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      data["lastRemoteStreamCreatedTimestamp"] = gpr_format_timespec(ts);
    }
  }
  int64_t streams_succeeded =
      streams_succeeded_.load(std::memory_order_relaxed);
  if (streams_succeeded != 0) {
    data["streamsSucceeded"] = std::to_string(streams_succeeded);
  }
  int64_t streams_failed = streams_failed_.load(std::memory_order_relaxed);
  if (streams_failed != 0) {
    data["streamsFailed"] = std::to_string(streams_failed);
  }
  int64_t messages_sent = messages_sent_.load(std::memory_order_relaxed);
  if (messages_sent != 0) {
    data["messagesSent"] = std::to_string(messages_sent);
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            last_message_sent_cycle_.load(std::memory_order_relaxed)),
        GPR_CLOCK_REALTIME);
    data["lastMessageSentTimestamp"] = gpr_format_timespec(ts);
  }
  int64_t messages_received =
      messages_received_.load(std::memory_order_relaxed);
  if (messages_received != 0) {
    data["messagesReceived"] = std::to_string(messages_received);
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            last_message_received_cycle_.load(std::memory_order_relaxed)),
        GPR_CLOCK_REALTIME);
    data["lastMessageReceivedTimestamp"] = gpr_format_timespec(ts);
  }
  int64_t keepalives_sent = keepalives_sent_.load(std::memory_order_relaxed);
  if (keepalives_sent != 0) {
    data["keepAlivesSent"] = std::to_string(keepalives_sent);
  }
  // Create and fill the parent object.
  Json::Object object = {
      {"ref",
       Json::Object{
           {"socketId", std::to_string(uuid())},
           {"name", name()},
       }},
      {"data", std::move(data)},
  };
  if (security_ != nullptr &&
      security_->type != SocketNode::Security::ModelType::kUnset) {
    object["security"] = security_->RenderJson();
  }
  PopulateSocketAddressJson(&object, "remote", remote_.c_str());
  PopulateSocketAddressJson(&object, "local", local_.c_str());
  return object;
}

//
// ListenSocketNode
//

ListenSocketNode::ListenSocketNode(std::string local_addr, std::string name)
    : BaseNode(EntityType::kSocket, std::move(name)),
      local_addr_(std::move(local_addr)) {}

Json ListenSocketNode::RenderJson() {
  Json::Object object = {
      {"ref",
       Json::Object{
           {"socketId", std::to_string(uuid())},
           {"name", name()},
       }},
  };
  PopulateSocketAddressJson(&object, "local", local_addr_.c_str());
  return object;
}

}  // namespace channelz
}  // namespace grpc_core
