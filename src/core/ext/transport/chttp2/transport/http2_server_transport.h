//
//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SERVER_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SERVER_TRANSPORT_H

#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace http2 {
class Http2ServerTransport final : public ServerTransport {
 public:
  Http2ServerTransport(
      const ChannelArgs& args, PromiseEndpoint control_endpoint,
      PromiseEndpoint data_endpoint,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      HPackParser hpack_parser, HPackCompressor hpack_encoder) {}

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return nullptr; }
  ServerTransport* server_transport() override { return this; }
  absl::string_view GetTransportName() const override { return "http2"; }

  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}

  void SetCallDestination(
      RefCountedPtr<UnstartedCallDestination> call_destination) override;
  void PerformOp(grpc_transport_op*) override;

  void AbortWithError();
  void Orphan() override;

 private:
};
}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SERVER_TRANSPORT_H
