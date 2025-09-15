// Copyright 2025 gRPC authors.
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

#include "test/cpp/sleuth/client.h"

#include <grpcpp/create_channel.h>

#include <vector>

#include "src/core/transport/endpoint_transport.h"

namespace grpc_sleuth {

Client::Client(std::string target, Options options)
    : channel_(grpc::CreateCustomChannel(target, options.creds,
                                         MakeChannelArguments(options))),
      stub_(grpc::channelz::v2::Channelz::NewStub(channel_)) {}

grpc::ChannelArguments Client::MakeChannelArguments(const Options& options) {
  grpc::ChannelArguments args;
  args.SetString(GRPC_ARG_PREFERRED_TRANSPORT_PROTOCOLS, options.protocol);
  return args;
}

absl::StatusOr<std::vector<grpc::channelz::v2::Entity>>
Client::QueryAllChannelzEntities() {
  grpc::ClientContext context;
  grpc::channelz::v2::QueryEntitiesRequest request;
  grpc::channelz::v2::QueryEntitiesResponse response;
  std::vector<grpc::channelz::v2::Entity> entities;
  while (true) {
    grpc::Status status = stub_->QueryEntities(&context, request, &response);
    if (!status.ok()) {
      return absl::Status(static_cast<absl::StatusCode>(status.error_code()),
                          status.error_message());
    }
    for (const auto& entity : response.entities()) {
      entities.push_back(entity);
    }
    if (response.end()) {
      break;
    }
    if (response.entities().empty()) {
      return absl::InternalError(
          "channelz pagination issue: received no entities but not end of "
          "list");
    }
    request.set_start_entity_id(entities.back().id() + 1);
  }
  return entities;
}

absl::Status Client::QueryTrace(
    int64_t entity_id, absl::string_view trace_name,
    absl::FunctionRef<
        void(size_t, absl::Span<const grpc::channelz::v2::TraceEvent* const>)>
        callback) {
  grpc::ClientContext context;
  grpc::channelz::v2::QueryTraceRequest request;
  request.set_id(entity_id);
  request.set_name(trace_name);
  grpc::channelz::v2::QueryTraceResponse response;
  std::unique_ptr<grpc::ClientReader<grpc::channelz::v2::QueryTraceResponse>>
      reader = stub_->QueryTrace(&context, request);
  while (reader->Read(&response)) {
    callback(response.events().size() - response.num_events_matched(),
             response.events());
  }
  grpc::Status status = reader->Finish();
  if (!status.ok()) {
    // Manual conversion
    return absl::Status(static_cast<absl::StatusCode>(status.error_code()),
                        status.error_message());
  }
  return absl::OkStatus();
}

}  // namespace grpc_sleuth
