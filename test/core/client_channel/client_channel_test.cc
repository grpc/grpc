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

#include "src/core/client_channel/client_channel.h"

#include <atomic>
#include <memory>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/config/core_configuration.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

using EventEngine = grpc_event_engine::experimental::EventEngine;

namespace {
const absl::string_view kTestScheme = "test";
const absl::string_view kTestTarget = "/target";
const absl::string_view kTestPath = "/test_method";
std::string TestTarget() {
  return absl::StrCat(kTestScheme, "://", kTestTarget);
}
}  // namespace

class ClientChannelTest : public YodelTest {
 public:
 protected:
  using YodelTest::YodelTest;

  ClientChannel& InitChannel(const ChannelArgs& args) {
    auto channel = ClientChannel::Create(TestTarget(), CompleteArgs(args));
    CHECK_OK(channel);
    channel_ = OrphanablePtr<ClientChannel>(
        DownCast<ClientChannel*>(channel->release()));
    return *channel_;
  }

  ClientChannel& channel() { return *channel_; }

  ClientMetadataHandle MakeClientInitialMetadata() {
    auto client_initial_metadata = Arena::MakePooled<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString(kTestPath));
    return client_initial_metadata;
  }

  CallHandler TickUntilCallStarted() {
    return TickUntil<CallHandler>([this]() -> Poll<CallHandler> {
      auto handler = call_destination_->PopHandler();
      if (handler.has_value()) return std::move(*handler);
      return Pending();
    });
  }

  void QueueNameResolutionResult(Resolver::Result result) {
    if (resolver_ != nullptr) {
      resolver_->QueueNameResolutionResult(std::move(result));
    } else {
      early_resolver_results_.push(std::move(result));
    }
  }

  Resolver::Result MakeSuccessfulResolutionResult(
      absl::string_view endpoint_address) {
    Resolver::Result result;
    grpc_resolved_address address;
    CHECK(grpc_parse_uri(URI::Parse(endpoint_address).value(), &address));
    result.addresses = EndpointAddressesList({EndpointAddresses{address, {}}});
    return result;
  }

 private:
  class TestConnector final : public SubchannelConnector {
   public:
    void Connect(const Args& args, Result* result,
                 grpc_closure* notify) override {
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
      gpr_log(GPR_INFO, "CreateSubchannel: args=%s", args.ToString().c_str());
      return Subchannel::Create(MakeOrphanable<TestConnector>(), address, args);
    }
  };

  class TestCallDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      handlers_.push(
          unstarted_call_handler.V2HackToStartCallWithoutACallFilterStack());
    }

    absl::optional<CallHandler> PopHandler() {
      if (handlers_.empty()) return absl::nullopt;
      auto handler = std::move(handlers_.front());
      handlers_.pop();
      return handler;
    }

    void Orphaned() override {}

   private:
    std::queue<CallHandler> handlers_;
  };

  class TestCallDestinationFactory final
      : public ClientChannel::CallDestinationFactory {
   public:
    explicit TestCallDestinationFactory(ClientChannelTest* test)
        : test_(test) {}

    RefCountedPtr<UnstartedCallDestination> CreateCallDestination(
        ClientChannel::PickerObservable picker) override {
      CHECK(!test_->picker_.has_value());
      test_->picker_ = std::move(picker);
      return test_->call_destination_;
    }

   private:
    ClientChannelTest* const test_;
  };

  class TestResolver final : public Resolver {
   public:
    explicit TestResolver(
        ClientChannelTest* test, ChannelArgs args,
        std::unique_ptr<Resolver::ResultHandler> result_handler,
        std::shared_ptr<WorkSerializer> work_serializer)
        : test_(test),
          args_(std::move(args)),
          result_handler_(std::move(result_handler)),
          work_serializer_(std::move(work_serializer)) {
      CHECK(test_->resolver_ == nullptr);
      test_->resolver_ = this;
    }

    ~TestResolver() {
      CHECK(test_->resolver_ == this);
      test_->resolver_ = nullptr;
    }

    void StartLocked() override {
      while (!test_->early_resolver_results_.empty()) {
        QueueNameResolutionResult(
            std::move(test_->early_resolver_results_.front()));
        test_->early_resolver_results_.pop();
      }
    }
    void ShutdownLocked() override {}

    void QueueNameResolutionResult(Resolver::Result result) {
      result.args = result.args.UnionWith(args_);
      work_serializer_->Run(
          [self = RefAsSubclass<TestResolver>(),
           result = std::move(result)]() mutable {
            self->result_handler_->ReportResult(std::move(result));
          },
          DEBUG_LOCATION);
    }

   private:
    ClientChannelTest* const test_;
    const ChannelArgs args_;
    const std::unique_ptr<Resolver::ResultHandler> result_handler_;
    const std::shared_ptr<WorkSerializer> work_serializer_;
  };

  class TestResolverFactory final : public ResolverFactory {
   public:
    explicit TestResolverFactory(ClientChannelTest* test) : test_(test) {}

    OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
      CHECK_EQ(args.uri.scheme(), kTestScheme);
      CHECK_EQ(args.uri.path(), kTestTarget);
      return MakeOrphanable<TestResolver>(test_, std::move(args.args),
                                          std::move(args.result_handler),
                                          std::move(args.work_serializer));
    }

    absl::string_view scheme() const override { return "test"; }
    bool IsValidUri(const URI& uri) const override { return true; }

   private:
    ClientChannelTest* const test_;
  };

  ChannelArgs CompleteArgs(const ChannelArgs& args) {
    return args.SetObject(&call_destination_factory_)
        .SetObject(&client_channel_factory_)
        .SetObject(ResourceQuota::Default())
        .SetObject(std::static_pointer_cast<EventEngine>(event_engine()))
        // TODO(ctiller): remove once v3 supports retries?
        .SetIfUnset(GRPC_ARG_ENABLE_RETRIES, 0);
  }

  void InitCoreConfiguration() override {
    CoreConfiguration::RegisterBuilder(
        [this](CoreConfiguration::Builder* builder) {
          builder->resolver_registry()->RegisterResolverFactory(
              std::make_unique<TestResolverFactory>(this));
        });
  }

  void Shutdown() override {
    ExecCtx exec_ctx;
    channel_.reset();
    picker_.reset();
  }

  OrphanablePtr<ClientChannel> channel_;
  absl::optional<ClientChannel::PickerObservable> picker_;
  TestCallDestinationFactory call_destination_factory_{this};
  TestClientChannelFactory client_channel_factory_;
  RefCountedPtr<TestCallDestination> call_destination_ =
      MakeRefCounted<TestCallDestination>();
  // Resolver results that have been reported before the resolver has been
  // instantiated.
  std::queue<Resolver::Result> early_resolver_results_;
  TestResolver* resolver_ = nullptr;
};

#define CLIENT_CHANNEL_TEST(name) YODEL_TEST(ClientChannelTest, name)

CLIENT_CHANNEL_TEST(NoOp) { InitChannel(ChannelArgs()); }

CLIENT_CHANNEL_TEST(CreateCall) {
  auto& channel = InitChannel(ChannelArgs());
  auto call_initiator = channel.CreateCall(MakeClientInitialMetadata());
  SpawnTestSeq(call_initiator, "cancel", [call_initiator]() mutable {
    call_initiator.Cancel();
    return Empty{};
  });
  WaitForAllPendingWork();
}

CLIENT_CHANNEL_TEST(StartCall) {
  auto& channel = InitChannel(ChannelArgs());
  auto call_initiator = channel.CreateCall(MakeClientInitialMetadata());
  QueueNameResolutionResult(
      MakeSuccessfulResolutionResult("ipv4:127.0.0.1:1234"));
  auto call_handler = TickUntilCallStarted();
  SpawnTestSeq(call_initiator, "cancel", [call_initiator]() mutable {
    call_initiator.Cancel();
    return Empty{};
  });
  WaitForAllPendingWork();
}

}  // namespace grpc_core
