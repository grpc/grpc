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

#include "src/core/ext/transport/chaotic_good/chaotic_good.h"

#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"

namespace grpc_core::chaotic_good {

namespace {

class ChaoticGoodEndpointTransport final : public EndpointTransport {
 public:
  absl::StatusOr<grpc_channel*> ChannelCreate(
      std::string target, const ChannelArgs& args) override {
    return CreateChaoticGoodChannel(std::move(target), args);
  }

  absl::StatusOr<int> AddPort(Server* server, std::string addr,
                              const ChannelArgs& args) override {
    return AddChaoticGoodPort(server, std::move(addr), args);
  }
};

const absl::string_view kWireFormatPreferences = []() {
  // Side-effect: registers the transport with the config system.
  CoreConfiguration::RegisterPersistentBuilder(
      [](CoreConfiguration::Builder* builder) {
        builder->endpoint_transport_registry()->RegisterTransport(
            "cg3", std::make_unique<ChaoticGoodEndpointTransport>());
      });
  return "cg3";
}();

}  // namespace

absl::string_view WireFormatPreferences() { return kWireFormatPreferences; }

}  // namespace grpc_core::chaotic_good
