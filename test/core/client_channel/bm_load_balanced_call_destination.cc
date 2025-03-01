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

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "src/core/client_channel/load_balanced_call_destination.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "test/core/call/call_spine_benchmarks.h"

namespace grpc_core {
namespace {

const Slice kTestPath = Slice::FromExternalString("/foo/bar");

class LoadBalancedCallDestinationTraits {
 public:
  RefCountedPtr<UnstartedCallDestination> CreateCallDestination(
      RefCountedPtr<UnstartedCallDestination> final_destination) {
    picker_observable_.Set(MakeRefCounted<TestPicker>(
        MakeRefCounted<TestSubchannel>(std::move(final_destination))));
    return MakeRefCounted<LoadBalancedCallDestination>(picker_observable_);
  }

  ClientMetadataHandle MakeClientInitialMetadata() {
    auto md = Arena::MakePooled<ClientMetadata>();
    md->Set(HttpPathMetadata(), kTestPath.Copy());
    return md;
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return Arena::MakePooled<ServerMetadata>();
  }

  MessageHandle MakePayload() { return Arena::MakePooled<Message>(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    return Arena::MakePooled<ServerMetadata>();
  }

 private:
  class TestSubchannel : public SubchannelInterfaceWithCallDestination {
   public:
    explicit TestSubchannel(
        RefCountedPtr<UnstartedCallDestination> call_destination)
        : call_destination_(std::move(call_destination)) {}

    void WatchConnectivityState(
        std::unique_ptr<ConnectivityStateWatcherInterface>) override {
      Crash("not implemented");
    }
    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface*) override {
      Crash("not implemented");
    }
    void RequestConnection() override { Crash("not implemented"); }
    void ResetBackoff() override { Crash("not implemented"); }
    void AddDataWatcher(std::unique_ptr<DataWatcherInterface>) override {
      Crash("not implemented");
    }
    void CancelDataWatcher(DataWatcherInterface*) override {
      Crash("not implemented");
    }
    RefCountedPtr<UnstartedCallDestination> call_destination() override {
      return call_destination_;
    }

    std::string address() const override { return "test"; }

   private:
    const RefCountedPtr<UnstartedCallDestination> call_destination_;
  };

  class TestPicker final : public LoadBalancingPolicy::SubchannelPicker {
   public:
    explicit TestPicker(RefCountedPtr<TestSubchannel> subchannel)
        : subchannel_{subchannel} {}

    LoadBalancingPolicy::PickResult Pick(
        LoadBalancingPolicy::PickArgs) override {
      return LoadBalancingPolicy::PickResult::Complete(subchannel_);
    }

   private:
    RefCountedPtr<TestSubchannel> subchannel_;
  };

  ClientChannel::PickerObservable picker_observable_{nullptr};
};
GRPC_CALL_SPINE_BENCHMARK(
    UnstartedCallDestinationFixture<LoadBalancedCallDestinationTraits>);

void BM_LoadBalancedCallDestination(benchmark::State& state) {
  class FinalDestination : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler) override {}
    void Orphaned() override {}
  };
  LoadBalancedCallDestinationTraits traits;
  auto final_destination = MakeRefCounted<FinalDestination>();
  for (auto _ : state) {
    traits.CreateCallDestination(final_destination);
  }
}
BENCHMARK(BM_LoadBalancedCallDestination);

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
