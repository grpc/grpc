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
#include "src/core/client_channel/client_channel.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "test/core/call/call_spine_benchmarks.h"

namespace grpc_core {

namespace {
const Slice kTestPath = Slice::FromExternalString("/foo/bar");
}

class ClientChannelTraits {
 public:
  RefCountedPtr<UnstartedCallDestination> CreateCallDestination(
      RefCountedPtr<UnstartedCallDestination> final_destination) {
    call_destination_factory_ = std::make_unique<TestCallDestinationFactory>(
        std::move(final_destination));
    auto channel = ClientChannel::Create(
        "test:///target",
        ChannelArgs()
            .SetObject(&client_channel_factory_)
            .SetObject(call_destination_factory_.get())
            .SetObject(ResourceQuota::Default())
            .SetObject(grpc_event_engine::experimental::GetDefaultEventEngine())
            .Set(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, true)
            // TODO(ctiller): remove once v3 supports retries
            .Set(GRPC_ARG_ENABLE_RETRIES, 0));
    CHECK_OK(channel);
    return std::move(*channel);
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

 private:
  class TestConnector final : public SubchannelConnector {
   public:
    void Connect(const Args&, Result*, grpc_closure* notify) override {
      CHECK_EQ(notify_, nullptr);
      notify_ = notify;
    }

    void Shutdown(grpc_error_handle error) override {
      if (notify_ != nullptr) ExecCtx::Run(DEBUG_LOCATION, notify_, error);
    }

   private:
    grpc_closure* notify_ = nullptr;
  };

  class TestClientChannelFactory final : public ClientChannelFactory {
   public:
    RefCountedPtr<Subchannel> CreateSubchannel(
        const grpc_resolved_address& address,
        const ChannelArgs& args) override {
      return Subchannel::Create(MakeOrphanable<TestConnector>(), address, args);
    }
  };

  class TestCallDestinationFactory final
      : public ClientChannel::CallDestinationFactory {
   public:
    explicit TestCallDestinationFactory(
        RefCountedPtr<UnstartedCallDestination> call_destination)
        : call_destination_(std::move(call_destination)) {}

    RefCountedPtr<UnstartedCallDestination> CreateCallDestination(
        ClientChannel::PickerObservable picker) override {
      return call_destination_;
    }

   private:
    RefCountedPtr<UnstartedCallDestination> call_destination_;
  };

  std::unique_ptr<TestCallDestinationFactory> call_destination_factory_;
  TestClientChannelFactory client_channel_factory_;
};
GRPC_CALL_SPINE_BENCHMARK(UnstartedCallDestinationFixture<ClientChannelTraits>);

namespace {
class TestResolver final : public Resolver {
 public:
  explicit TestResolver(ChannelArgs args,
                        std::unique_ptr<Resolver::ResultHandler> result_handler,
                        std::shared_ptr<WorkSerializer> work_serializer)
      : args_(std::move(args)),
        result_handler_(std::move(result_handler)),
        work_serializer_(std::move(work_serializer)) {}

  void StartLocked() override {
    work_serializer_->Run([self = RefAsSubclass<TestResolver>()] {
      self->result_handler_->ReportResult(
          self->MakeSuccessfulResolutionResult("ipv4:127.0.0.1:1234"));
    });
  }
  void ShutdownLocked() override {}

 private:
  Resolver::Result MakeSuccessfulResolutionResult(
      absl::string_view endpoint_address) {
    Resolver::Result result;
    result.args = args_;
    grpc_resolved_address address;
    CHECK(grpc_parse_uri(URI::Parse(endpoint_address).value(), &address));
    result.addresses = EndpointAddressesList({EndpointAddresses{address, {}}});
    return result;
  }

  const ChannelArgs args_;
  const std::unique_ptr<Resolver::ResultHandler> result_handler_;
  const std::shared_ptr<WorkSerializer> work_serializer_;
};

class TestResolverFactory final : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return MakeOrphanable<TestResolver>(std::move(args.args),
                                        std::move(args.result_handler),
                                        std::move(args.work_serializer));
  }

  absl::string_view scheme() const override { return "test"; }
  bool IsValidUri(const URI&) const override { return true; }
};

void BM_CreateClientChannel(benchmark::State& state) {
  class FinalDestination : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler) override {}
    void Orphaned() override {}
  };
  ClientChannelTraits traits;
  auto final_destination = MakeRefCounted<FinalDestination>();
  for (auto _ : state) {
    traits.CreateCallDestination(final_destination);
  }
}
BENCHMARK(BM_CreateClientChannel);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->resolver_registry()->RegisterResolverFactory(
            std::make_unique<grpc_core::TestResolverFactory>());
      });
  grpc_init();
  {
    auto ee = grpc_event_engine::experimental::GetDefaultEventEngine();
    benchmark::RunTheBenchmarksNamespaced();
  }
  grpc_shutdown();
  return 0;
}
