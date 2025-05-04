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

#include "src/core/transport/endpoint_transport_client_channel_factory.h"

#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/transport_credentials.h"

namespace grpc_core::endpoint_transport_client_channel_factory_detail {

RefCountedPtr<Subchannel> GenericClientChannelFactory::CreateSubchannel(
    const grpc_resolved_address& address, const ChannelArgs& args) {
  absl::StatusOr<ChannelArgs> new_args = GetSecureNamingChannelArgs(args);
  if (!new_args.ok()) {
    LOG(ERROR) << "Failed to create channel args during subchannel creation: "
               << new_args.status() << "; Got args: " << args.ToString();
    return nullptr;
  }
  return Subchannel::Create(MakeConnector(), address, *new_args);
}

absl::StatusOr<ChannelArgs>
GenericClientChannelFactory::GetSecureNamingChannelArgs(ChannelArgs args) {
  auto* channel_credentials = args.GetObject<grpc_channel_credentials>();
  if (channel_credentials == nullptr) {
    return absl::InternalError("channel credentials missing for channel");
  }
  // Make sure security connector does not already exist in args.
  if (args.Contains(GRPC_ARG_SECURITY_CONNECTOR)) {
    return absl::InternalError(
        "security connector already present in channel args.");
  }
  // Find the authority to use in the security connector.
  std::optional<std::string> authority =
      args.GetOwnedString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (!authority.has_value()) {
    return absl::InternalError("authority not present in channel args");
  }
  // Create the security connector using the credentials and target name.
  RefCountedPtr<grpc_channel_security_connector> subchannel_security_connector =
      channel_credentials->create_security_connector(
          /*call_creds=*/nullptr, authority->c_str(), &args);
  if (subchannel_security_connector == nullptr) {
    return absl::InternalError(absl::StrFormat(
        "Failed to create subchannel for secure name '%s'", *authority));
  }
  return args.SetObject(std::move(subchannel_security_connector));
}

}  // namespace grpc_core::endpoint_transport_client_channel_factory_detail
