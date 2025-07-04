//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/channelz/channelz.h"

#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <tuple>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "src/core/channelz/channelz_registry.h"
#include "src/core/channelz/property_list.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/notification.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"
#include "src/core/util/time_precise.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/uri.h"
#include "src/core/util/useful.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"

namespace grpc_core {
namespace channelz {

//
// DataSink
//

void DataSinkImplementation::AddData(absl::string_view name,
                                     std::unique_ptr<Data> data) {
  MutexLock lock(&mu_);
  additional_info_.emplace(name, std::move(data));
}

Json::Object DataSinkImplementation::Finalize(bool) {
  MutexLock lock(&mu_);
  Json::Object out;
  for (auto& [name, additional_info] : additional_info_) {
    out[name] = Json::FromObject(additional_info->ToJson());
  }
  return out;
}

void DataSinkImplementation::Finalize(bool timed_out,
                                      grpc_channelz_v2_Entity* entity,
                                      upb_Arena* arena) {
  MutexLock lock(&mu_);
  grpc_channelz_v2_Entity_set_timed_out(entity, timed_out);
  for (auto& [name, additional_info] : additional_info_) {
    auto* staple = grpc_channelz_v2_Entity_add_data(entity, arena);
    grpc_channelz_v2_Data_set_name(staple,
                                   CopyStdStringToUpbString(name, arena));
    additional_info->FillProto(
        grpc_channelz_v2_Data_mutable_value(staple, arena), arena);
  }
}

//
// BaseNode
//

BaseNode::BaseNode(EntityType type, size_t max_trace_memory, std::string name)
    : type_(type),
      uuid_(-1),
      name_(std::move(name)),
      trace_(max_trace_memory) {}

void BaseNode::NodeConstructed() {
  node_constructed_called_ = true;
  ChannelzRegistry::Register(this);
}

void BaseNode::Orphaned() {
  DCHECK(node_constructed_called_);
  ChannelzRegistry::Unregister(this);
}

intptr_t BaseNode::UuidSlow() { return ChannelzRegistry::NumberNode(this); }

std::string BaseNode::RenderJsonString() {
  Json json = RenderJson();
  return JsonDump(json);
}

void BaseNode::PopulateJsonFromDataSources(Json::Object& json) {
  auto info = AdditionalInfo();
  if (info.empty()) return;
  json["additionalInfo"] = Json::FromObject(std::move(info));
}

Json::Object BaseNode::AdditionalInfo() {
  auto done = std::make_shared<Notification>();
  auto sink_impl = std::make_shared<DataSinkImplementation>();
  {
    MutexLock lock(&data_sources_mu_);
    auto done_notifier = std::make_shared<DataSinkCompletionNotification>(
        [done]() { done->Notify(); });
    for (DataSource* data_source : data_sources_) {
      data_source->AddData(DataSink(sink_impl, done_notifier));
    }
  }
  bool completed =
      done->WaitForNotificationWithTimeout(absl::Milliseconds(100));
  return sink_impl->Finalize(!completed);
}

void BaseNode::RunZTrace(
    absl::string_view name, Timestamp deadline,
    std::map<std::string, std::string> args,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    absl::AnyInvocable<void(Json)> callback) {
  // Limit deadline to help contain potential resource exhaustion due to
  // tracing.
  deadline = std::min(deadline, Timestamp::Now() + Duration::Minutes(10));
  auto fail = [&callback, event_engine](absl::Status status) {
    event_engine->Run(
        [callback = std::move(callback), status = std::move(status)]() mutable {
          Json::Object object;
          object["status"] = Json::FromString(status.ToString());
          callback(Json::FromObject(std::move(object)));
        });
  };
  std::unique_ptr<ZTrace> ztrace;
  {
    MutexLock lock(&data_sources_mu_);
    for (auto* data_source : data_sources_) {
      if (auto found_ztrace = data_source->GetZTrace(name);
          found_ztrace != nullptr) {
        if (ztrace == nullptr) {
          ztrace = std::move(found_ztrace);
        } else {
          fail(absl::InternalError(
              absl::StrCat("Ambiguous ztrace handler: ", name)));
          return;
        }
      }
    }
  }
  if (ztrace == nullptr) {
    fail(absl::NotFoundError(absl::StrCat("ztrace not found: ", name)));
    return;
  }
  ztrace->Run(deadline, std::move(args), event_engine, std::move(callback));
}

void BaseNode::SerializeEntity(grpc_channelz_v2_Entity* entity,
                               upb_Arena* arena) {
  grpc_channelz_v2_Entity_set_id(entity, uuid());
  grpc_channelz_v2_Entity_set_kind(
      entity, StdStringToUpbString(EntityTypeToKind(type_)));
  {
    MutexLock lock(&parent_mu_);
    auto* parents =
        grpc_channelz_v2_Entity_resize_parents(entity, parents_.size(), arena);
    for (const auto& parent : parents_) {
      *parents++ = parent->uuid();
    }
  }
  grpc_channelz_v2_Entity_set_orphaned(entity, orphaned_index_ != 0);

  auto done = std::make_shared<Notification>();
  auto sink_impl = std::make_shared<DataSinkImplementation>();
  auto done_notifier = std::make_shared<DataSinkCompletionNotification>(
      [done]() { done->Notify(); });
  auto make_data_sink = [sink_impl, done_notifier]() {
    return DataSink(sink_impl, done_notifier);
  };
  AddNodeSpecificData(make_data_sink());
  {
    MutexLock lock(&data_sources_mu_);
    for (DataSource* data_source : data_sources_) {
      data_source->AddData(make_data_sink());
    }
  }
  bool completed =
      done->WaitForNotificationWithTimeout(absl::Milliseconds(100));
  sink_impl->Finalize(!completed, entity, arena);

  trace_.Render(entity, arena);
}

void BaseNode::AddNodeSpecificData(DataSink) {
  // Default implementation does nothing.
}

std::string BaseNode::SerializeEntityToString() {
  upb_Arena* arena = upb_Arena_New();
  auto cleanup = absl::MakeCleanup([arena]() { upb_Arena_Free(arena); });
  grpc_channelz_v2_Entity* entity = grpc_channelz_v2_Entity_new(arena);
  SerializeEntity(entity, arena);
  size_t length;
  auto* bytes = grpc_channelz_v2_Entity_serialize(entity, arena, &length);
  return std::string(bytes, length);
}

//
// DataSource
//

DataSource::DataSource(RefCountedPtr<BaseNode> node) : node_(std::move(node)) {}

DataSource::~DataSource() {
  DCHECK(node_ == nullptr) << "DataSource must be ResetDataSource()'d in the "
                              "most derived class before destruction";
}

void DataSource::SourceConstructed() {
  if (node_ == nullptr) return;
  MutexLock lock(&node_->data_sources_mu_);
  node_->data_sources_.push_back(this);
}

void DataSource::SourceDestructing() {
  RefCountedPtr<BaseNode> node = std::move(node_);
  if (node == nullptr) return;
  MutexLock lock(&node->data_sources_mu_);
  for (size_t i = 0; i < node->data_sources_.size(); ++i) {
    if (node->data_sources_[i] == this) {
      std::swap(node->data_sources_[i], node->data_sources_.back());
      node->data_sources_.pop_back();
      return;
    }
  }
  LOG(DFATAL) << "DataSource not found in node's data sources -- probably "
                 "SourceConstructed was not called";
}

//
// CallCountingHelper
//

void CallCountingHelper::RecordCallStarted() {
  calls_started_.fetch_add(1, std::memory_order_relaxed);
  last_call_started_cycle_.store(gpr_get_cycle_counter(),
                                 std::memory_order_relaxed);
}

void CallCountingHelper::RecordCallFailed() {
  calls_failed_.fetch_add(1, std::memory_order_relaxed);
}

void CallCountingHelper::RecordCallSucceeded() {
  calls_succeeded_.fetch_add(1, std::memory_order_relaxed);
}

//
// CallCounts
//

void CallCounts::PopulateJson(Json::Object& json) const {
  if (calls_started != 0) {
    json["callsStarted"] = Json::FromString(absl::StrCat(calls_started));
    json["lastCallStartedTimestamp"] =
        Json::FromString(last_call_started_timestamp());
  }
  if (calls_succeeded != 0) {
    json["callsSucceeded"] = Json::FromString(absl::StrCat(calls_succeeded));
  }
  if (calls_failed != 0) {
    json["callsFailed"] = Json::FromString(absl::StrCat(calls_failed));
  }
}

PropertyList CallCounts::ToPropertyList() const {
  return PropertyList()
      .Set("calls_started", calls_started)
      .Set("calls_succeeded", calls_succeeded)
      .Set("calls_failed", calls_failed)
      .Set("last_call_started_timestamp", [this]() -> std::optional<Timestamp> {
        if (last_call_started_cycle == 0) return std::nullopt;
        return Timestamp::FromCycleCounterRoundDown(last_call_started_cycle);
      }());
}

//
// PerCpuCallCountingHelper
//

void PerCpuCallCountingHelper::RecordCallStarted() {
  auto& data = per_cpu_data_.this_cpu();
  data.calls_started.fetch_add(1, std::memory_order_relaxed);
  data.last_call_started_cycle.store(gpr_get_cycle_counter(),
                                     std::memory_order_relaxed);
}

void PerCpuCallCountingHelper::RecordCallFailed() {
  per_cpu_data_.this_cpu().calls_failed.fetch_add(1, std::memory_order_relaxed);
}

void PerCpuCallCountingHelper::RecordCallSucceeded() {
  per_cpu_data_.this_cpu().calls_succeeded.fetch_add(1,
                                                     std::memory_order_relaxed);
}

CallCounts PerCpuCallCountingHelper::GetCallCounts() const {
  CallCounts call_counts;
  for (const auto& cpu : per_cpu_data_) {
    call_counts.calls_started +=
        cpu.calls_started.load(std::memory_order_relaxed);
    call_counts.calls_succeeded +=
        cpu.calls_succeeded.load(std::memory_order_relaxed);
    call_counts.calls_failed +=
        cpu.calls_failed.load(std::memory_order_relaxed);
    call_counts.last_call_started_cycle =
        std::max(call_counts.last_call_started_cycle,
                 cpu.last_call_started_cycle.load(std::memory_order_relaxed));
  }
  return call_counts;
}

//
// ChannelNode
//

ChannelNode::ChannelNode(std::string target, size_t max_trace_memory,
                         bool is_internal_channel)
    : BaseNode(is_internal_channel ? EntityType::kInternalChannel
                                   : EntityType::kTopLevelChannel,
               max_trace_memory, target),
      target_(std::move(target)) {
  NodeConstructed();
}

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

namespace {

std::set<intptr_t> ChildIdSet(const BaseNode* parent,
                              BaseNode::EntityType type) {
  std::set<intptr_t> ids;
  auto [children, _] = ChannelzRegistry::GetChildrenOfType(
      0, parent, type, std::numeric_limits<size_t>::max());
  for (const auto& node : children) {
    ids.insert(node->uuid());
  }
  return ids;
}

}  // namespace

std::set<intptr_t> ChannelNode::child_channels() const {
  return ChildIdSet(this, BaseNode::EntityType::kInternalChannel);
}

std::set<intptr_t> ChannelNode::child_subchannels() const {
  return ChildIdSet(this, BaseNode::EntityType::kSubchannel);
}

std::optional<std::string> ChannelNode::connectivity_state() {
  // Connectivity state.
  // If low-order bit is on, then the field is set.
  int state_field = connectivity_state_.load(std::memory_order_relaxed);
  if ((state_field & 1) != 0) {
    grpc_connectivity_state state =
        static_cast<grpc_connectivity_state>(state_field >> 1);
    return ConnectivityStateName(state);
  }
  return std::nullopt;
}

Json ChannelNode::RenderJson() {
  Json::Object data = {
      {"target", Json::FromString(target_)},
  };
  if (auto cs = connectivity_state(); cs.has_value()) {
    data["state"] = Json::FromObject({
        {"state", Json::FromString(cs.value())},
    });
  }
  // Fill in the channel trace if applicable.
  Json trace_json = trace().RenderJson();
  if (trace_json.type() != Json::Type::kNull) {
    data["trace"] = std::move(trace_json);
  }
  // Ask CallCountingHelper to populate call count data.
  call_counter_.GetCallCounts().PopulateJson(data);
  // Construct outer object.
  Json::Object json = {
      {"ref", Json::FromObject({
                  {"channelId", Json::FromString(absl::StrCat(uuid()))},
              })},
      {"data", Json::FromObject(std::move(data))},
  };
  // Template method. Child classes may override this to add their specific
  // functionality.
  PopulateChildRefs(&json);
  PopulateJsonFromDataSources(json);
  return Json::FromObject(std::move(json));
}

void ChannelNode::AddNodeSpecificData(DataSink sink) {
  sink.AddData("channel", PropertyList()
                              .Set("target", target_)
                              .Set("connectivity_state", connectivity_state()));
  sink.AddData("call_counts", call_counter_.GetCallCounts().ToPropertyList());
  sink.AddData("channel_args", channel_args_.ToPropertyList());
}

void ChannelNode::PopulateChildRefs(Json::Object* json) {
  auto child_subchannels = this->child_subchannels();
  auto child_channels = this->child_channels();
  if (!child_subchannels.empty()) {
    Json::Array array;
    for (intptr_t subchannel_uuid : child_subchannels) {
      array.emplace_back(Json::FromObject({
          {"subchannelId", Json::FromString(absl::StrCat(subchannel_uuid))},
      }));
    }
    (*json)["subchannelRef"] = Json::FromArray(std::move(array));
  }
  if (!child_channels.empty()) {
    Json::Array array;
    for (intptr_t channel_uuid : child_channels) {
      array.emplace_back(Json::FromObject({
          {"channelId", Json::FromString(absl::StrCat(channel_uuid))},
      }));
    }
    (*json)["channelRef"] = Json::FromArray(std::move(array));
  }
}

void ChannelNode::SetConnectivityState(grpc_connectivity_state state) {
  // Store with low-order bit set to indicate that the field is set.
  int state_field = (state << 1) + 1;
  connectivity_state_.store(state_field, std::memory_order_relaxed);
}

//
// SubchannelNode
//

SubchannelNode::SubchannelNode(std::string target_address,
                               size_t max_trace_memory)
    : BaseNode(EntityType::kSubchannel, max_trace_memory, target_address),
      target_(std::move(target_address)) {
  NodeConstructed();
}

SubchannelNode::~SubchannelNode() {}

void SubchannelNode::UpdateConnectivityState(grpc_connectivity_state state) {
  connectivity_state_.store(state, std::memory_order_relaxed);
}

void SubchannelNode::SetChildSocket(RefCountedPtr<SocketNode> socket) {
  MutexLock lock(&socket_mu_);
  child_socket_ =
      socket == nullptr ? nullptr : socket->WeakRefAsSubclass<SocketNode>();
}

std::string SubchannelNode::connectivity_state() const {
  grpc_connectivity_state state =
      connectivity_state_.load(std::memory_order_relaxed);
  return ConnectivityStateName(state);
}

Json SubchannelNode::RenderJson() {
  // Create and fill the data child.
  Json::Object data = {
      {"state", Json::FromObject({
                    {"state", Json::FromString(connectivity_state())},
                })},
      {"target", Json::FromString(target_)},
  };
  // Fill in the channel trace if applicable
  Json trace_json = trace().RenderJson();
  if (trace_json.type() != Json::Type::kNull) {
    data["trace"] = std::move(trace_json);
  }
  // Ask CallCountingHelper to populate call count data.
  call_counter_.GetCallCounts().PopulateJson(data);
  // Construct top-level object.
  Json::Object object{
      {"ref", Json::FromObject({
                  {"subchannelId", Json::FromString(absl::StrCat(uuid()))},
              })},
      {"data", Json::FromObject(std::move(data))},
  };
  // Populate the child socket.
  WeakRefCountedPtr<SocketNode> child_socket;
  {
    MutexLock lock(&socket_mu_);
    child_socket = child_socket_;
  }
  if (child_socket != nullptr && child_socket->uuid() != 0) {
    object["socketRef"] = Json::FromArray({
        Json::FromObject({
            {"socketId", Json::FromString(absl::StrCat(child_socket->uuid()))},
            {"name", Json::FromString(child_socket->name())},
        }),
    });
  }
  PopulateJsonFromDataSources(object);
  return Json::FromObject(std::move(object));
}

void SubchannelNode::AddNodeSpecificData(DataSink sink) {
  sink.AddData("channel", PropertyList()
                              .Set("target", target_)
                              .Set("connectivity_state", connectivity_state()));
  sink.AddData("call_counts", call_counter_.GetCallCounts().ToPropertyList());
  sink.AddData("channel_args", channel_args_.ToPropertyList());
}

//
// ServerNode
//

ServerNode::ServerNode(size_t max_trace_memory)
    : BaseNode(EntityType::kServer, max_trace_memory, "") {
  NodeConstructed();
}

ServerNode::~ServerNode() {}

std::string ServerNode::RenderServerSockets(intptr_t start_socket_id,
                                            intptr_t max_results) {
  CHECK_GE(start_socket_id, 0);
  CHECK_GE(max_results, 0);
  // If user does not set max_results, we choose 500.
  if (max_results == 0) max_results = 500;
  Json::Object object;
  auto [children, end] = ChannelzRegistry::GetChildrenOfType(
      start_socket_id, this, BaseNode::EntityType::kSocket, max_results);
  // Create list of socket refs.
  Json::Array array;
  for (const auto& child : children) {
    array.emplace_back(Json::FromObject({
        {"socketId", Json::FromString(absl::StrCat(child->uuid()))},
        {"name", Json::FromString(child->name())},
    }));
  }
  object["socketRef"] = Json::FromArray(std::move(array));
  if (end) object["end"] = Json::FromBool(true);
  return JsonDump(Json::FromObject(std::move(object)));
}

Json ServerNode::RenderJson() {
  Json::Object data;
  // Fill in the channel trace if applicable.
  Json trace_json = trace().RenderJson();
  if (trace_json.type() != Json::Type::kNull) {
    data["trace"] = std::move(trace_json);
  }
  // Ask CallCountingHelper to populate call count data.
  call_counter_.GetCallCounts().PopulateJson(data);
  // Construct top-level object.
  Json::Object object = {
      {"ref", Json::FromObject({
                  {"serverId", Json::FromString(absl::StrCat(uuid()))},
              })},
      {"data", Json::FromObject(std::move(data))},
  };
  // Render listen sockets.
  auto [children, _] = ChannelzRegistry::GetChildrenOfType(
      0, this, BaseNode::EntityType::kListenSocket,
      std::numeric_limits<size_t>::max());
  if (!children.empty()) {
    Json::Array array;
    for (const auto& child : children) {
      array.emplace_back(Json::FromObject({
          {"socketId", Json::FromString(absl::StrCat(child->uuid()))},
          {"name", Json::FromString(child->name())},
      }));
    }
    object["listenSocket"] = Json::FromArray(std::move(array));
  }
  PopulateJsonFromDataSources(object);
  return Json::FromObject(std::move(object));
}

void ServerNode::AddNodeSpecificData(DataSink sink) {
  sink.AddData("call_counts", call_counter_.GetCallCounts().ToPropertyList());
  sink.AddData("channel_args", channel_args_.ToPropertyList());
}

std::map<intptr_t, WeakRefCountedPtr<ListenSocketNode>>
ServerNode::child_listen_sockets() const {
  std::map<intptr_t, WeakRefCountedPtr<ListenSocketNode>> result;
  auto [children, _] = ChannelzRegistry::GetChildrenOfType(
      0, this, BaseNode::EntityType::kListenSocket,
      std::numeric_limits<size_t>::max());
  for (const auto& child : children) {
    result[child->uuid()] = child->WeakRefAsSubclass<ListenSocketNode>();
  }
  return result;
}

std::map<intptr_t, WeakRefCountedPtr<SocketNode>> ServerNode::child_sockets()
    const {
  std::map<intptr_t, WeakRefCountedPtr<SocketNode>> result;
  auto [children, _] = ChannelzRegistry::GetChildrenOfType(
      0, this, BaseNode::EntityType::kSocket,
      std::numeric_limits<size_t>::max());
  for (const auto& child : children) {
    result[child->uuid()] = child->WeakRefAsSubclass<SocketNode>();
  }
  return result;
}

//
// SocketNode::Security::Tls
//

Json SocketNode::Security::Tls::RenderJson() {
  Json::Object data;
  if (type == NameType::kStandardName) {
    data["standard_name"] = Json::FromString(name);
  } else if (type == NameType::kOtherName) {
    data["other_name"] = Json::FromString(name);
  }
  if (!local_certificate.empty()) {
    data["local_certificate"] =
        Json::FromString(absl::Base64Escape(local_certificate));
  }
  if (!remote_certificate.empty()) {
    data["remote_certificate"] =
        Json::FromString(absl::Base64Escape(remote_certificate));
  }
  return Json::FromObject(std::move(data));
}

PropertyList SocketNode::Security::Tls::ToPropertyList() const {
  PropertyList result;
  switch (type) {
    case NameType::kUnset:
      break;
    case NameType::kStandardName:
      result.Set("standard_name", name);
      break;
    case NameType::kOtherName:
      result.Set("other_name", name);
      break;
  }
  if (!local_certificate.empty()) {
    result.Set("local_certificate", absl::Base64Escape(local_certificate));
  }
  if (!remote_certificate.empty()) {
    result.Set("remote_certificate", absl::Base64Escape(remote_certificate));
  }
  return result;
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
      if (other.has_value()) {
        data["other"] = *other;
      }
      break;
  }
  return Json::FromObject(std::move(data));
}

PropertyList SocketNode::Security::ToPropertyList() const {
  switch (type) {
    case ModelType::kUnset:
      break;
    case ModelType::kTls:
      if (tls) {
        return tls->ToPropertyList();
      }
      break;
    case ModelType::kOther:
      if (other.has_value()) {
        return PropertyList().Set("other", JsonDump(*other));
      }
      break;
  }
  return PropertyList();
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
        (*json)[name] = Json::FromObject({
            {"tcpip_address",
             Json::FromObject({
                 {"port", Json::FromString(
                              absl::StrCat(grpc_sockaddr_get_port(&*address)))},
                 {"ip_address",
                  Json::FromString(absl::Base64Escape(packed_host))},
             })},
        });
        return;
      }
    } else if (uri->scheme() == "unix") {
      (*json)[name] = Json::FromObject({
          {"uds_address", Json::FromObject({
                              {"filename", Json::FromString(uri->path())},
                          })},
      });
      return;
    }
  }
  // Unknown address type.
  (*json)[name] = Json::FromObject({
      {"other_address", Json::FromObject({
                            {"name", Json::FromString(addr_str)},
                        })},
  });
}

}  // namespace

SocketNode::SocketNode(std::string local, std::string remote, std::string name,
                       RefCountedPtr<Security> security)
    : BaseNode(EntityType::kSocket, 0, std::move(name)),
      local_(std::move(local)),
      remote_(std::move(remote)),
      security_(std::move(security)) {
  NodeConstructed();
}

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
    data["streamsStarted"] = Json::FromString(absl::StrCat(streams_started));
    gpr_cycle_counter last_local_stream_created_cycle =
        last_local_stream_created_cycle_.load(std::memory_order_relaxed);
    if (last_local_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_local_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      data["lastLocalStreamCreatedTimestamp"] =
          Json::FromString(gpr_format_timespec(ts));
    }
    gpr_cycle_counter last_remote_stream_created_cycle =
        last_remote_stream_created_cycle_.load(std::memory_order_relaxed);
    if (last_remote_stream_created_cycle != 0) {
      ts = gpr_convert_clock_type(
          gpr_cycle_counter_to_time(last_remote_stream_created_cycle),
          GPR_CLOCK_REALTIME);
      data["lastRemoteStreamCreatedTimestamp"] =
          Json::FromString(gpr_format_timespec(ts));
    }
  }
  int64_t streams_succeeded =
      streams_succeeded_.load(std::memory_order_relaxed);
  if (streams_succeeded != 0) {
    data["streamsSucceeded"] =
        Json::FromString(absl::StrCat(streams_succeeded));
  }
  int64_t streams_failed = streams_failed_.load(std::memory_order_relaxed);
  if (streams_failed != 0) {
    data["streamsFailed"] = Json::FromString(absl::StrCat(streams_failed));
  }
  int64_t messages_sent = messages_sent_.load(std::memory_order_relaxed);
  if (messages_sent != 0) {
    data["messagesSent"] = Json::FromString(absl::StrCat(messages_sent));
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            last_message_sent_cycle_.load(std::memory_order_relaxed)),
        GPR_CLOCK_REALTIME);
    data["lastMessageSentTimestamp"] =
        Json::FromString(gpr_format_timespec(ts));
  }
  int64_t messages_received =
      messages_received_.load(std::memory_order_relaxed);
  if (messages_received != 0) {
    data["messagesReceived"] =
        Json::FromString(absl::StrCat(messages_received));
    ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(
            last_message_received_cycle_.load(std::memory_order_relaxed)),
        GPR_CLOCK_REALTIME);
    data["lastMessageReceivedTimestamp"] =
        Json::FromString(gpr_format_timespec(ts));
  }
  int64_t keepalives_sent = keepalives_sent_.load(std::memory_order_relaxed);
  if (keepalives_sent != 0) {
    data["keepAlivesSent"] = Json::FromString(absl::StrCat(keepalives_sent));
  }
  // Create and fill the parent object.
  Json::Object object = {
      {"ref", Json::FromObject({
                  {"socketId", Json::FromString(absl::StrCat(uuid()))},
                  {"name", Json::FromString(name())},
              })},
      {"data", Json::FromObject(std::move(data))},
  };
  if (security_ != nullptr &&
      security_->type != SocketNode::Security::ModelType::kUnset) {
    object["security"] = security_->RenderJson();
  }
  PopulateSocketAddressJson(&object, "remote", remote_.c_str());
  PopulateSocketAddressJson(&object, "local", local_.c_str());
  PopulateJsonFromDataSources(object);
  return Json::FromObject(std::move(object));
}

void SocketNode::AddNodeSpecificData(DataSink sink) {
  auto convert_cycle_counter =
      [](gpr_cycle_counter cycle_counter) -> std::optional<Timestamp> {
    if (cycle_counter == 0) return std::nullopt;
    return Timestamp::FromCycleCounterRoundDown(cycle_counter);
  };
  sink.AddData("socket",
               PropertyList().Set("local", local_).Set("remote", remote_));
  sink.AddData(
      "call_counts",
      PropertyList()
          .Set("streams_started",
               streams_started_.load(std::memory_order_relaxed))
          .Set("streams_succeeded",
               streams_succeeded_.load(std::memory_order_relaxed))
          .Set("streams_failed",
               streams_failed_.load(std::memory_order_relaxed))
          .Set("messages_sent", messages_sent_.load(std::memory_order_relaxed))
          .Set("messages_received",
               messages_received_.load(std::memory_order_relaxed))
          .Set("keepalives_sent",
               keepalives_sent_.load(std::memory_order_relaxed))
          .Set("last_local_stream_created_timestamp",
               convert_cycle_counter(last_local_stream_created_cycle_.load(
                   std::memory_order_relaxed)))
          .Set("last_remote_stream_created_timestamp",
               convert_cycle_counter(last_remote_stream_created_cycle_.load(
                   std::memory_order_relaxed)))
          .Set("last_message_sent_timestamp",
               convert_cycle_counter(
                   last_message_sent_cycle_.load(std::memory_order_relaxed)))
          .Set("last_message_received_timestamp",
               convert_cycle_counter(last_message_received_cycle_.load(
                   std::memory_order_relaxed))));
  if (security_ != nullptr) {
    sink.AddData("security", security_->ToPropertyList());
  }
}

//
// ListenSocketNode
//

ListenSocketNode::ListenSocketNode(std::string local_addr, std::string name)
    : BaseNode(EntityType::kListenSocket, 0, std::move(name)),
      local_addr_(std::move(local_addr)) {
  NodeConstructed();
}

Json ListenSocketNode::RenderJson() {
  Json::Object object = {
      {"ref", Json::FromObject({
                  {"socketId", Json::FromString(absl::StrCat(uuid()))},
                  {"name", Json::FromString(name())},
              })},
  };
  PopulateSocketAddressJson(&object, "local", local_addr_.c_str());
  PopulateJsonFromDataSources(object);
  return Json::FromObject(std::move(object));
}

void ListenSocketNode::AddNodeSpecificData(DataSink sink) {
  sink.AddData("listen_socket", PropertyList().Set("local", local_addr_));
}

//
// CallNode
//

Json CallNode::RenderJson() {
  Json::Object object = {
      {"ref", Json::FromObject({
                  {"callId", Json::FromString(absl::StrCat(uuid()))},
              })},
  };
  PopulateJsonFromDataSources(object);
  return Json::FromObject(std::move(object));
}

}  // namespace channelz
}  // namespace grpc_core
