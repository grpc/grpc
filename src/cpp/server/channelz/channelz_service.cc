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

#include "absl/strings/str_cat.h"
#include "src/core/channelz/channelz_registry.h"
#include "src/core/channelz/v2tov1/convert.h"
#include "src/core/lib/experiments/experiments.h"

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

grpc::protobuf::util::Status ParseJson(const char* json_str,
                                       grpc::protobuf::Message* message) {
  grpc::protobuf::json::JsonParseOptions options;
  options.case_insensitive_enum_parsing = true;
  auto r =
      grpc::protobuf::json::JsonStringToMessage(json_str, message, options);
  if (!r.ok()) {
    LOG(ERROR) << "channelz json parse failed: error=" << r.ToString()
               << " json:\n"
               << json_str;
  }
  return r;
}

}  // namespace

Status ChannelzService::GetTopChannels(
    ServerContext* /*unused*/,
    const channelz::v1::GetTopChannelsRequest* request,
    channelz::v1::GetTopChannelsResponse* response) {
  if (grpc_core::IsChannelzUseV2ForV1ServiceEnabled()) {
    auto [channels, end] =
        grpc_core::channelz::ChannelzRegistry::GetTopChannels(
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
        return Status(StatusCode::INTERNAL,
                      "Failed to parse converted channel");
      }
    }
    response->set_end(end);
  } else {
    char* json_str =
        grpc_channelz_get_top_channels(request->start_channel_id());
    if (json_str == nullptr) {
      return Status(StatusCode::INTERNAL,
                    "grpc_channelz_get_top_channels returned null");
    }
    grpc::protobuf::util::Status s = ParseJson(json_str, response);
    gpr_free(json_str);
    if (!s.ok()) {
      return Status(StatusCode::INTERNAL, s.ToString());
    }
  }
  return Status::OK;
}

Status ChannelzService::GetServers(
    ServerContext* /*unused*/, const channelz::v1::GetServersRequest* request,
    channelz::v1::GetServersResponse* response) {
  if (grpc_core::IsChannelzUseV2ForV1ServiceEnabled()) {
    auto [servers, end] = grpc_core::channelz::ChannelzRegistry::GetServers(
        request->start_server_id());
    RegistryEntityFetcher fetcher;
    for (const auto& server_node : servers) {
      if (server_node == nullptr) continue;
      auto serialized_v2 =
          server_node->SerializeEntityToString(kChannelzTimeout);
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
  } else {
    char* json_str = grpc_channelz_get_servers(request->start_server_id());
    if (json_str == nullptr) {
      return Status(StatusCode::INTERNAL,
                    "grpc_channelz_get_servers returned null");
    }
    grpc::protobuf::util::Status s = ParseJson(json_str, response);
    gpr_free(json_str);
    if (!s.ok()) {
      return Status(StatusCode::INTERNAL, s.ToString());
    }
  }
  return Status::OK;
}

Status ChannelzService::GetServer(ServerContext* /*unused*/,
                                  const channelz::v1::GetServerRequest* request,
                                  channelz::v1::GetServerResponse* response) {
  if (grpc_core::IsChannelzUseV2ForV1ServiceEnabled()) {
    auto server_node =
        grpc_core::channelz::ChannelzRegistry::GetServer(request->server_id());
    if (server_node == nullptr) {
      return Status(StatusCode::NOT_FOUND, "No object found for that ServerId");
    }
    RegistryEntityFetcher fetcher;
    auto serialized_v2 = server_node->SerializeEntityToString(kChannelzTimeout);
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertServer(
        serialized_v2, fetcher, false);
    if (!serialized_v1.ok()) {
      return Status(StatusCode::INTERNAL,
                    std::string(serialized_v1.status().message()));
    }
    if (!response->mutable_server()->ParseFromString(*serialized_v1)) {
      return Status(StatusCode::INTERNAL, "Failed to parse converted server");
    }
  } else {
    char* json_str = grpc_channelz_get_server(request->server_id());
    if (json_str == nullptr) {
      return Status(StatusCode::INTERNAL,
                    "grpc_channelz_get_server returned null");
    }
    grpc::protobuf::util::Status s = ParseJson(json_str, response);
    gpr_free(json_str);
    if (!s.ok()) {
      return Status(StatusCode::INTERNAL, s.ToString());
    }
  }
  return Status::OK;
}

Status ChannelzService::GetServerSockets(
    ServerContext* /*unused*/,
    const channelz::v1::GetServerSocketsRequest* request,
    channelz::v1::GetServerSocketsResponse* response) {
  if (grpc_core::IsChannelzUseV2ForV1ServiceEnabled()) {
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
      auto serialized_v2 =
          socket_node->SerializeEntityToString(kChannelzTimeout);
      auto converted = grpc_core::channelz::v2tov1::ConvertSocket(
          serialized_v2, fetcher, false);
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
  } else {
    char* json_str = grpc_channelz_get_server_sockets(
        request->server_id(), request->start_socket_id(),
        request->max_results());
    if (json_str == nullptr) {
      return Status(StatusCode::INTERNAL,
                    "grpc_channelz_get_server_sockets returned null");
    }
    grpc::protobuf::util::Status s = ParseJson(json_str, response);
    gpr_free(json_str);
    if (!s.ok()) {
      return Status(StatusCode::INTERNAL, s.ToString());
    }
  }
  return Status::OK;
}

Status ChannelzService::GetChannel(
    ServerContext* /*unused*/, const channelz::v1::GetChannelRequest* request,
    channelz::v1::GetChannelResponse* response) {
  if (grpc_core::IsChannelzUseV2ForV1ServiceEnabled()) {
    auto channel_node = grpc_core::channelz::ChannelzRegistry::GetChannel(
        request->channel_id());
    if (channel_node == nullptr) {
      return Status(StatusCode::NOT_FOUND,
                    "No object found for that ChannelId");
    }
    RegistryEntityFetcher fetcher;
    auto serialized_v2 =
        channel_node->SerializeEntityToString(kChannelzTimeout);
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertChannel(
        serialized_v2, fetcher, false);
    if (!serialized_v1.ok()) {
      return Status(StatusCode::INTERNAL,
                    std::string(serialized_v1.status().message()));
    }
    if (!response->mutable_channel()->ParseFromString(*serialized_v1)) {
      return Status(StatusCode::INTERNAL, "Failed to parse converted channel");
    }
  } else {
    char* json_str = grpc_channelz_get_channel(request->channel_id());
    if (json_str == nullptr) {
      return Status(StatusCode::NOT_FOUND,
                    "No object found for that ChannelId");
    }
    grpc::protobuf::util::Status s = ParseJson(json_str, response);
    gpr_free(json_str);
    if (!s.ok()) {
      return Status(StatusCode::INTERNAL, s.ToString());
    }
  }
  return Status::OK;
}

Status ChannelzService::GetSubchannel(
    ServerContext* /*unused*/,
    const channelz::v1::GetSubchannelRequest* request,
    channelz::v1::GetSubchannelResponse* response) {
  if (grpc_core::IsChannelzUseV2ForV1ServiceEnabled()) {
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
      return Status(StatusCode::INTERNAL,
                    "Failed to parse converted subchannel");
    }
  } else {
    char* json_str = grpc_channelz_get_subchannel(request->subchannel_id());
    if (json_str == nullptr) {
      return Status(StatusCode::NOT_FOUND,
                    "No object found for that SubchannelId");
    }
    grpc::protobuf::util::Status s = ParseJson(json_str, response);
    gpr_free(json_str);
    if (!s.ok()) {
      return Status(StatusCode::INTERNAL, s.ToString());
    }
  }
  return Status::OK;
}

Status ChannelzService::GetSocket(ServerContext* /*unused*/,
                                  const channelz::v1::GetSocketRequest* request,
                                  channelz::v1::GetSocketResponse* response) {
  if (grpc_core::IsChannelzUseV2ForV1ServiceEnabled()) {
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
      if (!response->mutable_socket()->mutable_ref()->ParseFromString(
              *serialized_v1)) {
        return Status(StatusCode::INTERNAL,
                      "Failed to parse converted listen socket");
      }
    } else {
      return Status(StatusCode::NOT_FOUND, "No object found for that SocketId");
    }
  } else {
    char* json_str = grpc_channelz_get_socket(request->socket_id());
    if (json_str == nullptr) {
      return Status(StatusCode::NOT_FOUND, "No object found for that SocketId");
    }
    grpc::protobuf::util::Status s = ParseJson(json_str, response);
    gpr_free(json_str);
    if (!s.ok()) {
      return Status(StatusCode::INTERNAL, s.ToString());
    }
  }
  return Status::OK;
}

Status ChannelzV2Service::QueryEntities(
    ServerContext* /*unused*/,
    const channelz::v2::QueryEntitiesRequest* request,
    channelz::v2::QueryEntitiesResponse* response) {
  std::optional<BaseNode::EntityType> type =
      BaseNode::KindToEntityType(request->kind());
  if (!type.has_value()) {
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
  const auto [nodes, end] =
      parent != nullptr
          ? grpc_core::channelz::ChannelzRegistry::GetChildrenOfType(
                request->start_entity_id(), parent.get(), *type, kMaxResults)
          : grpc_core::channelz::ChannelzRegistry::GetNodesOfType(
                request->start_entity_id(), *type, kMaxResults);
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

}  // namespace grpc
