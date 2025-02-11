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

#include <grpc/grpc.h>

#include <atomic>
#include <memory>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/service_config/service_config_impl.h"
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
 protected:
  using YodelTest::YodelTest;

  ClientChannel& InitChannel(const ChannelArgs& args) {
    auto channel = ClientChannel::Create(TestTarget(), CompleteArgs(args));
    CHECK_OK(channel);
    channel_ = RefCountedPtr<ClientChannel>(
        DownCast<ClientChannel*>(channel->release()));
    return *channel_;
  }

  ClientChannel& channel() { return *channel_; }

  ClientMetadataHandle MakeClientInitialMetadata() {
    auto client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
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
      absl::string_view endpoint_address,
      absl::StatusOr<RefCountedPtr<ServiceConfig>> service_config = nullptr,
      RefCountedPtr<ConfigSelector> config_selector = nullptr) {
    Resolver::Result result;
    grpc_resolved_address address;
    CHECK(grpc_parse_uri(URI::Parse(endpoint_address).value(), &address));
    result.addresses = EndpointAddressesList({EndpointAddresses{address, {}}});
    result.service_config = std::move(service_config);
    if (config_selector != nullptr) {
      CHECK(result.service_config.ok())
          << "channel does not use ConfigSelector without service config";
      CHECK(*result.service_config != nullptr)
          << "channel does not use ConfigSelector without service config";
      result.args = ChannelArgs().SetObject(std::move(config_selector));
    }
    return result;
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
      LOG(INFO) << "CreateSubchannel: args=" << args.ToString();
      return Subchannel::Create(MakeOrphanable<TestConnector>(), address, args);
    }
  };

  class TestCallDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      handlers_.push(unstarted_call_handler.StartCall());
    }

    std::optional<CallHandler> PopHandler() {
      if (handlers_.empty()) return std::nullopt;
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

    ~TestResolver() override {
      CHECK_EQ(test_->resolver_, this);
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
      work_serializer_->Run([self = RefAsSubclass<TestResolver>(),
                             result = std::move(result)]() mutable {
        self->result_handler_->ReportResult(std::move(result));
      });
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
    bool IsValidUri(const URI&) const override { return true; }

   private:
    ClientChannelTest* const test_;
  };

  ChannelArgs CompleteArgs(const ChannelArgs& args) {
    return args.SetObject(&call_destination_factory_)
        .SetObject(&client_channel_factory_)
        .SetObject(ResourceQuota::Default())
        .SetObject(std::static_pointer_cast<EventEngine>(event_engine()))
        .SetIfUnset(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, true)
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
    call_destination_.reset();
  }

  RefCountedPtr<ClientChannel> channel_;
  std::optional<ClientChannel::PickerObservable> picker_;
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

CLIENT_CHANNEL_TEST(StartCall) {
  auto& channel = InitChannel(ChannelArgs());
  auto arena = channel.call_arena_allocator()->MakeArena();
  arena->SetContext<EventEngine>(channel.event_engine());
  auto call = MakeCallPair(MakeClientInitialMetadata(), std::move(arena));
  channel.StartCall(std::move(call.handler));
  QueueNameResolutionResult(
      MakeSuccessfulResolutionResult("ipv4:127.0.0.1:1234"));
  auto call_handler = TickUntilCallStarted();
  SpawnTestSeq(
      call.initiator, "cancel",
      [call_initiator = call.initiator]() mutable { call_initiator.Cancel(); });
  WaitForAllPendingWork();
}

// A filter that adds metadata foo=bar.
class TestFilter {
 public:
  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md) {
      md.Append("foo", Slice::FromStaticString("bar"),
                [](absl::string_view error, const Slice&) {
                  FAIL() << "error encoding metadata: " << error;
                });
    }

    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;
  };

  static absl::StatusOr<std::unique_ptr<TestFilter>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return std::make_unique<TestFilter>();
  }
};

const NoInterceptor TestFilter::Call::OnClientToServerMessage;
const NoInterceptor TestFilter::Call::OnClientToServerHalfClose;
const NoInterceptor TestFilter::Call::OnServerInitialMetadata;
const NoInterceptor TestFilter::Call::OnServerToClientMessage;
const NoInterceptor TestFilter::Call::OnServerTrailingMetadata;
const NoInterceptor TestFilter::Call::OnFinalize;

// A config selector that adds TestFilter as a dynamic filter.
class TestConfigSelector : public ConfigSelector {
 public:
  UniqueTypeName name() const override {
    static UniqueTypeName::Factory kFactory("test");
    return kFactory.Create();
  }

  void AddFilters(InterceptionChainBuilder& builder) override {
    builder.Add<TestFilter>();
  }

  absl::Status GetCallConfig(GetCallConfigArgs /*args*/) override {
    return absl::OkStatus();
  }

  // Any instance of this class will behave the same, so all comparisons
  // are true.
  bool Equals(const ConfigSelector* /*other*/) const override { return true; }
};

CLIENT_CHANNEL_TEST(ConfigSelectorWithDynamicFilters) {
  auto& channel = InitChannel(ChannelArgs());
  auto arena = channel.call_arena_allocator()->MakeArena();
  arena->SetContext<EventEngine>(channel.event_engine());
  auto call = MakeCallPair(MakeClientInitialMetadata(), std::move(arena));
  channel.StartCall(std::move(call.handler));
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), "{}");
  ASSERT_TRUE(service_config.ok());
  QueueNameResolutionResult(MakeSuccessfulResolutionResult(
      "ipv4:127.0.0.1:1234", std::move(service_config),
      MakeRefCounted<TestConfigSelector>()));
  auto call_handler = TickUntilCallStarted();
  SpawnTestSeq(
      call_handler, "check_initial_metadata",
      [call_handler]() mutable {
        return call_handler.PullClientInitialMetadata();
      },
      [](ValueOrFailure<ClientMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        if (md.ok()) {
          std::string buffer;
          auto value = (*md)->GetStringValue("foo", &buffer);
          EXPECT_TRUE(value.has_value());
          if (value.has_value()) EXPECT_EQ(*value, "bar");
        }
      });
  SpawnTestSeq(
      call.initiator, "cancel",
      [call_initiator = call.initiator]() mutable { call_initiator.Cancel(); });
  WaitForAllPendingWork();
}

// TODO(ctiller, roth): MANY more test cases
// - Resolver returns an error for the initial result, then returns a valid
// result.
// - Resolver returns a service config (various permutations).

}  // namespace grpc_core
