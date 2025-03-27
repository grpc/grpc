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

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>

#include "absl/strings/string_view.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/call/call_spine_benchmarks.h"

namespace grpc_core {

class HttpClientFilterTraits {
 public:
  using Filter = HttpClientFilter;

  ChannelArgs MakeChannelArgs() { return ChannelArgs().SetObject(&transport_); }

  ClientMetadataHandle MakeClientInitialMetadata() {
    return Arena::MakePooledForOverwrite<ClientMetadata>();
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return Arena::MakePooledForOverwrite<ServerMetadata>();
  }

  MessageHandle MakePayload() { return Arena::MakePooled<Message>(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
    md->Set(HttpStatusMetadata(), 200);
    return md;
  }

 private:
  class FakeTransport final : public Transport {
   public:
    FilterStackTransport* filter_stack_transport() override { return nullptr; }
    ClientTransport* client_transport() override { return nullptr; }
    ServerTransport* server_transport() override { return nullptr; }
    absl::string_view GetTransportName() const override { return "fake-http"; }
    void SetPollset(grpc_stream*, grpc_pollset*) override {}
    void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
    void PerformOp(grpc_transport_op*) override {}
    void Orphan() override {}
  };

  FakeTransport transport_;
};
GRPC_CALL_SPINE_BENCHMARK(FilterFixture<HttpClientFilterTraits>);

}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  grpc_init();
  {
    auto ee = grpc_event_engine::experimental::GetDefaultEventEngine();
    benchmark::RunTheBenchmarksNamespaced();
  }
  grpc_shutdown();
  return 0;
}
