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

#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/ext/transport/chttp2/server/chttp2_server.h"
#include "src/core/transport/endpoint_transport.h"

namespace grpc_core {
namespace {
class Chttp2Transport final : public EndpointTransport {
 public:
  absl::StatusOr<grpc_channel*> ChannelCreate(
      std::string target, const ChannelArgs& args) override {
    return CreateHttp2Channel(target, args);
  }

  absl::StatusOr<int> AddPort(Server* server, std::string addr,
                              const ChannelArgs& args) override {
    return Chttp2ServerAddPort(server, addr.c_str(), args);
  }
};
}  // namespace

void RegisterChttp2Transport(CoreConfiguration::Builder* builder) {
  builder->endpoint_transport_registry()->RegisterTransport(
      "h2", std::make_unique<Chttp2Transport>());
}
}  // namespace grpc_core