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

#include <memory>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "test/core/call/call_spine_benchmarks.h"

namespace grpc_core {
namespace {

const Slice kTestPath = Slice::FromExternalString("/foo/bar");

class InprocTraits {
 public:
  BenchmarkTransport MakeTransport() {
    auto channel_args = CoreConfiguration::Get()
                            .channel_args_preconditioning()
                            .PreconditionChannelArgs(nullptr);
    auto t = MakeInProcessTransportPair(channel_args);
    return {OrphanablePtr<ClientTransport>(
                DownCast<ClientTransport*>(t.first.release())),
            OrphanablePtr<ServerTransport>(
                DownCast<ServerTransport*>(t.second.release()))};
  }

  ClientMetadataHandle MakeClientInitialMetadata() {
    auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
    md->Set(HttpPathMetadata(), kTestPath.Copy());
    return md;
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return Arena::MakePooledForOverwrite<ServerMetadata>();
  }

  MessageHandle MakePayload() { return Arena::MakePooled<Message>(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
    return md;
  }
};
GRPC_CALL_SPINE_BENCHMARK(TransportFixture<InprocTraits>);

}  // namespace
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
