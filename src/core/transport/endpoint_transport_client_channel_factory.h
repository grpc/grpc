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

#ifndef GRPC_SRC_CORE_TRANSPORT_ENDPOINT_TRANSPORT_CLIENT_CHANNEL_FACTORY_H
#define GRPC_SRC_CORE_TRANSPORT_ENDPOINT_TRANSPORT_CLIENT_CHANNEL_FACTORY_H

#include "src/core/client_channel/client_channel_factory.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/no_destruct.h"

namespace grpc_core {

namespace endpoint_transport_client_channel_factory_detail {

class GenericClientChannelFactory : public ClientChannelFactory {
 public:
  RefCountedPtr<Subchannel> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& args) override;

 private:
  virtual OrphanablePtr<SubchannelConnector> MakeConnector() = 0;

  static absl::StatusOr<ChannelArgs> GetSecureNamingChannelArgs(
      ChannelArgs args);
};

template <typename Connector>
class TypedClientChannelFactory final : public GenericClientChannelFactory {
 private:
  OrphanablePtr<SubchannelConnector> MakeConnector() override {
    return MakeOrphanable<Connector>();
  };
};

}  // namespace endpoint_transport_client_channel_factory_detail

template <typename Connector>
auto EndpointTransportClientChannelFactory() {
  return NoDestructSingleton<endpoint_transport_client_channel_factory_detail::
                                 TypedClientChannelFactory<Connector>>::Get();
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TRANSPORT_ENDPOINT_TRANSPORT_CLIENT_CHANNEL_FACTORY_H
