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

#ifndef GRPC_SRC_CORE_TRANSPORT_ENDPOINT_TRANSPORT_H
#define GRPC_SRC_CORE_TRANSPORT_ENDPOINT_TRANSPORT_H

#include <memory>
#include <string>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {

class Server;

// EndpointTransport is a transport that operates over EventEngine::Endpoint
// objects.
// This interface is an iterim thing whilst call-v3 is finished and we flesh
// out next protocol negotiation in all transport stacks. At that point this
// interface will change so that we can run many kinds of EndpointTransport
// on one listener, and negotiate protocol with one connector.
class EndpointTransport {
 public:
  virtual grpc_channel* ChannelCreate(std::string target,
                                      const ChannelArgs& args) = 0;
  virtual int AddPort(Server* server, std::string addr,
                      const ChannelArgs& args) = 0;
  virtual ~EndpointTransport();
};

class EndpointTransportRegistry {
 public:
  class Builder {
   public:
    void RegisterTransport(std::string name,
                           std::unique_ptr<EndpointTransport> transport) {
      transports_[name] = std::move(transport);
    }

    EndpointTransportRegistry Build() {
      return EndpointTransportRegistry(std::move(transports_));
    }

   private:
    std::map<std::string, std::unique_ptr<EndpointTransport>> transports_;
  };

  EndpointTransport* GetTransport(absl::string_view name);

 private:
  explicit EndpointTransportRegistry(
      std::map<std::string, std::unique_ptr<EndpointTransport>> transports);

  std::map<std::string, std::unique_ptr<EndpointTransport>> transports_;
};

}  // namespace grpc_core

#endif
