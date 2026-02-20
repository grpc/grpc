//
//
// Copyright 2018 gRPC authors.
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

#include "src/cpp/server/channelz/channelz_service.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/impl/codegen/config_protobuf.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "src/core/channelz/channelz.h"
#include "src/core/channelz/channelz_registry.h"
#include "src/core/channelz/v2tov1/convert.h"
#include "src/core/util/notification.h"
#include "absl/strings/str_cat.h"

using grpc_core::channelz::BaseNode;

namespace grpc {

namespace {

constexpr size_t kMaxResults = 100;
constexpr absl::Duration kChannelzTimeout = absl::Milliseconds(100);

class RegistryEntityFetcher
    : public grpc_core::channelz::v2tov1::EntityFetcher {
 public:
  absl::StatusOr<std::string> GetEntity(int64_t id) override {
    auto node = grpc_core::channelz::ChannelzRegistry::GetNode(id);
    if (node == nullptr) {
      return absl::NotFoundError(absl::StrCat("Entity not found: ", id));
    }
    return node->SerializeEntityToString(kChannelzTimeout);
  }

  absl::StatusOr<std::vector<std::string>> GetEntitiesWithParent(
      int64_t parent_id) override {
    auto node = grpc_core::channelz::ChannelzRegistry::GetNode(parent_id);
    if (node == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Parent entity not found: ", parent_id));
    }
    auto [nodes, end] = grpc_core::channelz::ChannelzRegistry::GetChildren(
        node.get(), 0, std::numeric_limits<size_t>::max());
    DCHECK(end);
    std::vector<std::string> children_str;
    for (const auto& node : nodes) {
      if (node == nullptr) continue;
      children_str.push_back(node->SerializeEntityToString(kChannelzTimeout));
    }
    return children_str;
  }
};

}  // namespace

Status ChannelzService::GetTopChannels(
    ServerContext* /*unused*/,
    const channelz::v1::GetTopChannelsRequest* request,
    channelz::v1::GetTopChannelsResponse* response) {
  auto [channels, end] = grpc_core::channelz::ChannelzRegistry::GetTopChannels(
      request->start_channel_id());
  RegistryEntityFetcher fetcher;
  for (const auto& channel_node : channels) {
    if (channel_node == nullptr) continue;
    auto serialized_v2 =
        channel_node->SerializeEntityToString(kChannelzTimeout);
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertChannel(
        serialized_v2, fetcher, false);
    if (!serialized_v1.ok()) {
      return Status(StatusCode::INTERNAL,
                    std::string(serialized_v1.status().message()));
    }
    if (!response->add_channel()->ParseFromString(*serialized_v1)) {
      return Status(StatusCode::INTERNAL, "Failed to parse converted channel");
    }
  }
  response->set_end(end);
  return Status::OK;
}

Status ChannelzService::GetServers(
    ServerContext* /*unused*/, const channelz::v1::GetServersRequest* request,
    channelz::v1::GetServersResponse* response) {
  auto [servers, end] = grpc_core::channelz::ChannelzRegistry::GetServers(
      request->start_server_id());
  RegistryEntityFetcher fetcher;
  for (const auto& server_node : servers) {
    if (server_node == nullptr) continue;
    auto serialized_v2 = server_node->SerializeEntityToString(kChannelzTimeout);
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertServer(
        serialized_v2, fetcher, false);
    if (!serialized_v1.ok()) {
      return Status(StatusCode::INTERNAL,
                    std::string(serialized_v1.status().message()));
    }
    if (!response->add_server()->ParseFromString(*serialized_v1)) {
      return Status(StatusCode::INTERNAL, "Failed to parse converted server");
    }
  }
  response->set_end(end);
  return Status::OK;
}

Status ChannelzService::GetServer(ServerContext* /*unused*/,
                                  const channelz::v1::GetServerRequest* request,
                                  channelz::v1::GetServerResponse* response) {
  auto server_node =
      grpc_core::channelz::ChannelzRegistry::GetServer(request->server_id());
  if (server_node == nullptr) {
    return Status(StatusCode::NOT_FOUND, "No object found for that ServerId");
  }
  RegistryEntityFetcher fetcher;
  auto serialized_v2 = server_node->SerializeEntityToString(kChannelzTimeout);
  auto serialized_v1 =
      grpc_core::channelz::v2tov1::ConvertServer(serialized_v2, fetcher, false);
  if (!serialized_v1.ok()) {
    return Status(StatusCode::INTERNAL,
                  std::string(serialized_v1.status().message()));
  }
  if (!response->mutable_server()->ParseFromString(*serialized_v1)) {
    return Status(StatusCode::INTERNAL, "Failed to parse converted server");
  }
  return Status::OK;
}

Status ChannelzService::GetServerSockets(
    ServerContext* /*unused*/,
    const channelz::v1::GetServerSocketsRequest* request,
    channelz::v1::GetServerSocketsResponse* response) {
  auto server_node =
      grpc_core::channelz::ChannelzRegistry::GetServer(request->server_id());
  if (server_node == nullptr) {
    return Status(StatusCode::NOT_FOUND, "No object found for that ServerId");
  }
  size_t max_results = request->max_results() == 0
                           ? kMaxResults
                           : static_cast<size_t>(request->max_results());
  RegistryEntityFetcher fetcher;
  auto [sockets, end] =
      grpc_core::channelz::ChannelzRegistry::GetChildrenOfType(
          request->start_socket_id(), server_node.get(),
          grpc_core::channelz::BaseNode::EntityType::kSocket, max_results);
  for (const auto& socket_node : sockets) {
    if (socket_node == nullptr) continue;
    auto serialized_v2 = socket_node->SerializeEntityToString(kChannelzTimeout);
    auto converted = grpc_core::channelz::v2tov1::ConvertSocket(serialized_v2,
                                                                fetcher, false);
    if (!converted.ok()) {
      return Status(StatusCode::INTERNAL,
                    std::string(converted.status().message()));
    }
    grpc::channelz::v1::Socket socket;
    if (!socket.ParseFromString(*converted)) {
      return Status(StatusCode::INTERNAL, "Failed to parse converted socket");
    }
    response->add_socket_ref()->CopyFrom(socket.ref());
  }
  response->set_end(end);
  return Status::OK;
}

Status ChannelzService::GetChannel(
    ServerContext* /*unused*/, const channelz::v1::GetChannelRequest* request,
    channelz::v1::GetChannelResponse* response) {
  auto channel_node =
      grpc_core::channelz::ChannelzRegistry::GetChannel(request->channel_id());
  if (channel_node == nullptr) {
    return Status(StatusCode::NOT_FOUND, "No object found for that ChannelId");
  }
  RegistryEntityFetcher fetcher;
  auto serialized_v2 = channel_node->SerializeEntityToString(kChannelzTimeout);
  auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertChannel(
      serialized_v2, fetcher, false);
  if (!serialized_v1.ok()) {
    return Status(StatusCode::INTERNAL,
                  std::string(serialized_v1.status().message()));
  }
  if (!response->mutable_channel()->ParseFromString(*serialized_v1)) {
    return Status(StatusCode::INTERNAL, "Failed to parse converted channel");
  }
  return Status::OK;
}

Status ChannelzService::GetSubchannel(
    ServerContext* /*unused*/,
    const channelz::v1::GetSubchannelRequest* request,
    channelz::v1::GetSubchannelResponse* response) {
  auto subchannel_node = grpc_core::channelz::ChannelzRegistry::GetSubchannel(
      request->subchannel_id());
  if (subchannel_node == nullptr) {
    return Status(StatusCode::NOT_FOUND,
                  "No object found for that SubchannelId");
  }
  RegistryEntityFetcher fetcher;
  auto serialized_v2 =
      subchannel_node->SerializeEntityToString(kChannelzTimeout);
  auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertSubchannel(
      serialized_v2, fetcher, false);
  if (!serialized_v1.ok()) {
    return Status(StatusCode::INTERNAL,
                  std::string(serialized_v1.status().message()));
  }
  if (!response->mutable_subchannel()->ParseFromString(*serialized_v1)) {
    return Status(StatusCode::INTERNAL, "Failed to parse converted subchannel");
  }
  return Status::OK;
}

Status ChannelzService::GetSocket(ServerContext* /*unused*/,
                                  const channelz::v1::GetSocketRequest* request,
                                  channelz::v1::GetSocketResponse* response) {
  auto node =
      grpc_core::channelz::ChannelzRegistry::GetNode(request->socket_id());
  if (node == nullptr) {
    return Status(StatusCode::NOT_FOUND, "No object found for that SocketId");
  }
  RegistryEntityFetcher fetcher;
  if (node->type() == grpc_core::channelz::BaseNode::EntityType::kSocket) {
    auto serialized_v2 = node->SerializeEntityToString(kChannelzTimeout);
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertSocket(
        serialized_v2, fetcher, false);
    if (!serialized_v1.ok()) {
      return Status(StatusCode::INTERNAL,
                    std::string(serialized_v1.status().message()));
    }
    if (!response->mutable_socket()->ParseFromString(*serialized_v1)) {
      return Status(StatusCode::INTERNAL, "Failed to parse converted socket");
    }
  } else if (node->type() ==
             grpc_core::channelz::BaseNode::EntityType::kListenSocket) {
    auto serialized_v2 = node->SerializeEntityToString(kChannelzTimeout);
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertListenSocket(
        serialized_v2, fetcher, false);
    if (!serialized_v1.ok()) {
      return Status(StatusCode::INTERNAL,
                    std::string(serialized_v1.status().message()));
    }
    if (!response->mutable_socket()->ParseFromString(*serialized_v1)) {
      return Status(StatusCode::INTERNAL,
                    "Failed to parse converted listen socket");
    }
  } else {
    return Status(StatusCode::NOT_FOUND, "No object found for that SocketId");
  }
  return Status::OK;
}

Status ChannelzV2Service::QueryEntities(
    ServerContext* /*unused*/,
    const channelz::v2::QueryEntitiesRequest* request,
    channelz::v2::QueryEntitiesResponse* response) {
  std::optional<BaseNode::EntityType> type =
      BaseNode::KindToEntityType(request->kind());
  if (!type.has_value() && !request->kind().empty()) {
    return Status(StatusCode::INVALID_ARGUMENT,
                  absl::StrCat("Invalid entity kind: ", request->kind()));
  }
  grpc_core::WeakRefCountedPtr<BaseNode> parent;
  if (request->parent() != 0) {
    parent = grpc_core::channelz::ChannelzRegistry::GetNode(request->parent());
    if (parent == nullptr) {
      return Status(StatusCode::NOT_FOUND,
                    "No object found for parent EntityId");
    }
  }
  const auto [nodes, end] = [&]() {
    if (parent != nullptr) {
      if (type.has_value()) {
        return grpc_core::channelz::ChannelzRegistry::GetChildrenOfType(
            request->start_entity_id(), parent.get(), *type, kMaxResults);
      } else {
        return grpc_core::channelz::ChannelzRegistry::GetNodes(
            request->start_entity_id(), kMaxResults);
      }
    } else {
      if (type.has_value()) {
        return grpc_core::channelz::ChannelzRegistry::GetNodesOfType(
            request->start_entity_id(), *type, kMaxResults);
      } else {
        return grpc_core::channelz::ChannelzRegistry::GetNodes(
            request->start_entity_id(), kMaxResults);
      }
    }
  }();
  response->set_end(end);
  for (const auto& node : nodes) {
    response->add_entities()->ParseFromString(
        node->SerializeEntityToString(kChannelzTimeout));
  }
  return Status::OK;
}

Status ChannelzV2Service::GetEntity(
    ServerContext* /*unused*/, const channelz::v2::GetEntityRequest* request,
    channelz::v2::GetEntityResponse* response) {
  auto node = grpc_core::channelz::ChannelzRegistry::GetNode(request->id());
  if (node == nullptr) {
    return Status(StatusCode::NOT_FOUND, "No object found for that EntityId");
  }
  response->mutable_entity()->ParseFromString(
      node->SerializeEntityToString(kChannelzTimeout));
  return Status::OK;
}

Status ChannelzV2Service::QueryTrace(
    ServerContext* /*ctx*/, const channelz::v2::QueryTraceRequest* request,
    ServerWriter<channelz::v2::QueryTraceResponse>* writer) {
  grpc_core::channelz::ZTrace::Args args;
  for (const auto& [key, value] : request->args()) {
    switch (value.value_case()) {
      case channelz::v2::QueryTraceRequest::QueryArgValue::kIntValue:
        args[key] = value.int_value();
        break;
      case channelz::v2::QueryTraceRequest::QueryArgValue::kStringValue:
        args[key] = value.string_value();
        break;
      case channelz::v2::QueryTraceRequest::QueryArgValue::kBoolValue:
        args[key] = value.bool_value();
        break;
      default:
        return Status(StatusCode::INVALID_ARGUMENT,
                      absl::StrCat("Invalid query arg value: ", value));
    }
  }
  auto node = grpc_core::channelz::ChannelzRegistry::GetNode(request->id());
  if (node == nullptr) {
    return Status(StatusCode::NOT_FOUND, "No object found for that EntityId");
  }
  struct State {
    grpc_core::Notification done;
    grpc_core::Mutex mu;
    grpc::Status status ABSL_GUARDED_BY(mu);
  };
  auto state = std::make_shared<State>();
  auto ztrace = node->RunZTrace(
      request->name(), std::move(args),
      grpc_event_engine::experimental::GetDefaultEventEngine(),
      [state, writer](absl::StatusOr<std::optional<std::string>> response) {
        if (state->done.HasBeenNotified()) return;
        grpc_core::MutexLock lock(&state->mu);
        if (!response.ok()) {
          state->status = grpc::Status(
              static_cast<grpc::StatusCode>(response.status().code()),
              std::string(response.status().message()));
          state->done.Notify();
          return;
        }
        if (!response->has_value()) {
          state->status = grpc::Status::OK;
          state->done.Notify();
          return;
        }
        channelz::v2::QueryTraceResponse r;
        r.ParseFromString(**response);
        if (!writer->Write(r)) {
          state->status = grpc::Status::CANCELLED;
          state->done.Notify();
        }
      });
  state->done.WaitForNotification();
  grpc_core::MutexLock lock(&state->mu);
  return state->status;
}

}  // namespace grpc
