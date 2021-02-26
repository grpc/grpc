//
//
// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/cpp/server/csds/csds.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include <string>

#include "src/core/ext/xds/xds_client.h"
#include "src/proto/grpc/testing/xds/v3/csds.grpc.pb.h"

namespace grpc {
namespace xds {
namespace experimental {

using envoy::service::status::v3::ClientConfig;
using envoy::service::status::v3::ClientStatusRequest;
using envoy::service::status::v3::ClientStatusResponse;

namespace {

grpc::protobuf::util::Status ParseJson(std::string json_str,
                                       grpc::protobuf::Message* message) {
  grpc::protobuf::json::JsonParseOptions options;
  options.case_insensitive_enum_parsing = true;
  return grpc::protobuf::json::JsonStringToMessage(json_str, message, options);
}

}  // namespace

absl::Status ClientStatusDiscoveryService::DumpClientConfig(
    ClientConfig* client_config) {
  grpc_error* error = GRPC_ERROR_NONE;
  auto xds_client = grpc_core::XdsClient::GetOrCreate(&error);
  if (error != GRPC_ERROR_NONE) {
    return absl::UnavailableError(
        absl::StrCat("XdsClient is not available:", grpc_error_string(error)));
  }
  std::string json_string = xds_client->DumpClientConfigInJson();
  grpc::protobuf::util::Status s = ParseJson(json_string, client_config);
  if (!s.ok()) {
    return absl::InternalError(
        absl::StrCat("Failed to parse ClientConfig:", s.ToString()));
  }
  return absl::OkStatus();
}

Status ClientStatusDiscoveryService::StreamClientStatus(
    ServerContext* /*context*/,
    ServerReaderWriter<ClientStatusResponse, ClientStatusRequest>* stream) {
  ClientStatusRequest request;
  while (stream->Read(&request)) {
    ClientStatusResponse response;
    ClientConfig* client_config = response.add_config();
    absl::Status s = DumpClientConfig(client_config);
    if (!s.ok()) {
      return Status(StatusCode(s.raw_code()), s.ToString());
    }
    stream->Write(response);
  }
  return Status::OK;
}

Status ClientStatusDiscoveryService::FetchClientStatus(
    ServerContext* /*context*/, const ClientStatusRequest* /*request*/,
    ClientStatusResponse* response) {
  ClientConfig* client_config = response->add_config();
  absl::Status s = DumpClientConfig(client_config);
  if (!s.ok()) {
    return Status(StatusCode(s.raw_code()), s.ToString());
  }
  return Status::OK;
}

}  // namespace experimental
}  // namespace xds
}  // namespace grpc
