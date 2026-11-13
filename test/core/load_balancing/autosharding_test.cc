//
// Copyright 2026 gRPC authors.
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

#include <grpc/grpc.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "proto/autosharding/v1/autosharding.pb.h"
#include "src/core/client_channel/channel_factory.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/load_balancing/autosharding/autosharding_client.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/weighted_target/weighted_target.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace grpc_core {
namespace testing {
namespace {

using ::google::cloud::autosharding::v1::WatchShardingAssignmentRequest;
using ::google::cloud::autosharding::v1::WatchShardingAssignmentResponse;
using ::testing::AnyOf;

constexpr absl::string_view kChannelFactoryKey = "sharding_service";

class FakeServerTarget final : public XdsBootstrap::XdsServerTarget {
 public:
  explicit FakeServerTarget(absl::string_view server_uri)
      : server_uri_(server_uri) {}

  const std::string& server_uri() const override { return server_uri_; }
  std::string Key() const override { return server_uri_; }
  bool Equals(const XdsServerTarget& other) const override {
    return Key() == other.Key();
  }

 private:
  std::string server_uri_;
};

// Returns a transport from FakeXdsTransportFactory for each channel factory
// key.  Like a real channel factory, it returns the same transport for the
// same key.
//
// Holds only a weak ref to the transport factory, because the channel args
// that hold this object can outlive the test fixture's ref, and the transport
// factory holds a ref to the EventEngine until it is orphaned.
class FakeChannelFactory final : public experimental::ChannelFactory {
 public:
  explicit FakeChannelFactory(
      WeakRefCountedPtr<FakeXdsTransportFactory> transport_factory)
      : transport_factory_(std::move(transport_factory)) {}

  std::unique_ptr<ChannelHandle> CreateChannel(absl::string_view key) override {
    ++num_create_channels_;
    absl::Status status;
    RefCountedPtr<XdsTransport> transport =
        transport_factory_->GetTransport(FakeServerTarget(key), &status);
    GRPC_CHECK(status.ok()) << status;
    return std::make_unique<ChannelHandleImpl>(std::move(transport));
  }

  int num_create_channels() const { return num_create_channels_; }

 private:
  WeakRefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  int num_create_channels_ = 0;
};

// Each slice is (end_key, endpoint_indices), starting at the previous end_key
// ("" for the first slice); an empty end_key denotes +infinity.
WatchShardingAssignmentResponse MakeChunkResponse(
    const std::vector<std::pair<absl::string_view, std::vector<int32_t>>>&
        slices,
    const std::vector<absl::string_view>& endpoint_names) {
  WatchShardingAssignmentResponse response;
  auto* chunk = response.mutable_chunk();
  absl::string_view start_key;
  for (const auto& [end_key, endpoint_indices] : slices) {
    auto* slice_assignment = chunk->add_slice_assignments();
    auto* slice = slice_assignment->mutable_slice();
    slice->set_start_key(start_key);
    if (!end_key.empty()) slice->set_end_key(end_key);
    for (int32_t index : endpoint_indices) {
      slice_assignment->add_endpoints()->set_endpoint_index(index);
    }
    start_key = end_key;
  }
  for (absl::string_view name : endpoint_names) {
    chunk->add_endpoints()->set_endpoint(name);
  }
  return response;
}

WatchShardingAssignmentResponse MakeMetadataResponse(int64_t generation) {
  WatchShardingAssignmentResponse response;
  response.mutable_metadata()->set_generation(generation);
  return response;
}

// Matches a std::optional<WatchShardingAssignmentRequest> that holds an
// InitialClientConfig request for target.
MATCHER_P(IsInitialClientConfig, target, "") {
  if (!arg.has_value()) {
    *result_listener << "no request";
    return false;
  }
  *result_listener << "request: " << arg->ShortDebugString();
  return arg->has_initial_client_config() &&
         arg->initial_client_config().target() == target;
}

class AutoShardingTest : public LoadBalancingPolicyTest {
 protected:
  static constexpr absl::string_view kKeyHeaderName = "x-slice-key";
  static constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"};

  class ConfigBuilder {
   public:
    ConfigBuilder() {
      SetChannelFactoryKey(std::string(kChannelFactoryKey));
      SetAutoshardingTarget("my_autosharding_target");
      SetKeyHeaderName(std::string(kKeyHeaderName));
      SetEnableFallback(false);
      SetInitialAssignmentTimeout(Duration::Seconds(1));
    }

    ConfigBuilder& SetChannelFactoryKey(std::string channel_factory_key) {
      json_["channelFactoryKey"] =
          Json::FromString(std::move(channel_factory_key));
      return *this;
    }
    ConfigBuilder& SetAutoshardingTarget(std::string autosharding_target) {
      json_["autoshardingTarget"] =
          Json::FromString(std::move(autosharding_target));
      return *this;
    }
    ConfigBuilder& SetKeyHeaderName(std::string key_header_name) {
      json_["keyHeaderName"] = Json::FromString(std::move(key_header_name));
      return *this;
    }
    ConfigBuilder& SetEnableFallback(bool enable_fallback) {
      json_["enableFallback"] = Json::FromBool(enable_fallback);
      return *this;
    }
    ConfigBuilder& SetInitialAssignmentTimeout(Duration duration) {
      json_["initialAssignmentTimeout"] =
          Json::FromString(duration.ToJsonString());
      return *this;
    }

    RefCountedPtr<LoadBalancingPolicy::Config> Build() {
      Json config = Json::FromArray({Json::FromObject(
          {{"autosharding_experimental", Json::FromObject(json_)}})});
      return MakeConfig(config);
    }

   private:
    Json::Object json_;
  };

  AutoShardingTest() : LoadBalancingPolicyTest("autosharding_experimental") {}

  // Returns the name that the autosharding server uses for the endpoint
  // with address, which is the address without the URI scheme.
  static absl::string_view EndpointName(absl::string_view address) {
    return absl::StripPrefix(address, "ipv4:");
  }

  static std::map<std::string, std::string> MakeMetadata(std::string key) {
    return {{std::string(kKeyHeaderName), std::move(key)}};
  }

  ChannelArgs MakeChannelArgs() {
    return ChannelArgs().SetObject<experimental::ChannelFactory>(factory_);
  }

  // Returns the stream that the client started on the channel for key, or
  // null if there is none.
  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> WaitForStream(
      absl::string_view key = kChannelFactoryKey) {
    return transport_factory_->WaitForStream(FakeServerTarget(key),
                                             AutoshardingClient::kMethod);
  }

  // Reads the next request that the client sent on stream.  Returns
  // std::nullopt if there is no request.  Crashes if the request cannot be
  // parsed.
  static std::optional<WatchShardingAssignmentRequest> ReadRequest(
      FakeXdsTransportFactory::FakeStreamingCall* stream) {
    std::optional<std::string> message = stream->WaitForMessageFromClient();
    if (!message.has_value()) return std::nullopt;
    WatchShardingAssignmentRequest request;
    GRPC_CHECK(request.ParseFromString(*message))
        << "cannot parse WatchShardingAssignmentRequest from client";
    return request;
  }

  void SendAssignment(
      const std::vector<std::pair<absl::string_view, std::vector<int32_t>>>&
          slices,
      const std::vector<absl::string_view>& endpoint_names,
      int64_t generation = 1, absl::string_view key = kChannelFactoryKey) {
    auto stream = WaitForStream(key);
    ASSERT_NE(stream, nullptr) << "client did not create a stream";
    stream->SendMessageToClient(
        MakeChunkResponse(slices, endpoint_names).SerializeAsString());
    stream->SendMessageToClient(
        MakeMetadataResponse(generation).SerializeAsString());
  }

  // Replaces the LB policy with a new one that reports to a new helper.
  void RecreateLbPolicy() {
    ExecCtx exec_ctx;
    DrainPendingWork();
    ExpectQueueEmpty();
    lb_policy_.reset();
    auto helper = std::make_unique<FakeHelper>(this);
    helper_ = helper.get();
    lb_policy_ =
        CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
            lb_policy_name_,
            LoadBalancingPolicy::Args{work_serializer_, std::move(helper),
                                      channel_args_});
    ASSERT_NE(lb_policy_, nullptr);
  }

  void SetUp() override {
    LoadBalancingPolicyTest::SetUp();
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { FAIL() << "Multiple concurrent reads"; }, fuzzing_ee_);
    // The tests do not read every ACK from the client.
    transport_factory_->SetAbortOnUndrainedMessages(false);
    factory_ = std::make_shared<FakeChannelFactory>(
        transport_factory_->WeakRefAsSubclass<FakeXdsTransportFactory>());
  }

  void TearDown() override {
    factory_.reset();
    transport_factory_.reset();
    LoadBalancingPolicyTest::TearDown();
  }

  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  std::shared_ptr<FakeChannelFactory> factory_;
};

TEST_F(AutoShardingTest, ServerSentEmptySliceDisablesFallback) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // Server sends ["", "m") to the first endpoint and ["m", +inf) with no
  // endpoints.
  SendAssignment({{"m", {0}}, {"", {}}}, {EndpointName(kAddresses[0])});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // A key in the server-sent-empty slice fails even though fallback is
  // enabled and the resolver has endpoints.
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status,
                  absl::UnavailableError(
                      "no endpoints assigned to this slice and fallback is "
                      "disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  // A key in the first slice triggers a connection attempt on the endpoint
  // assigned to it, and picks complete to that endpoint once it is READY.
  ExpectPickQueued(picker.get(), {}, MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  // The other endpoint is not assigned to any slice, so it is not used.
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                              MakeMetadata("a"));
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata("a"));
  EXPECT_THAT(address, ::testing::Optional(kAddresses[0]));
}

TEST_F(AutoShardingTest, GapUsesFallbackPool) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // The assignment covers only ["", "m"); the rest of the keyspace is a gap.
  SendAssignment({{"m", {0}}}, {EndpointName(kAddresses[0])});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // A key in the gap uses the fallback pool, so it queues a connection
  // attempt instead of failing.
  ExpectPickQueued(picker.get(), {}, MakeMetadata("z"));
  // The pick creates exactly one subchannel, and requests a connection on it.
  std::vector<SubchannelState*> subchannels;
  for (absl::string_view address : kAddresses) {
    SubchannelState* subchannel = FindSubchannel(address, args);
    if (subchannel != nullptr) subchannels.push_back(subchannel);
  }
  ASSERT_EQ(subchannels.size(), 1u) << "pick created more than one subchannel";
  EXPECT_TRUE(subchannels[0]->ConnectionRequested());
}

TEST_F(AutoShardingTest, GapFailsWhenFallbackDisabled) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(),
                                    MakeChannelArgs()),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SendAssignment({{"m", {0}}}, {EndpointName(kAddresses[0])});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError("no endpoint available"));
      },
      {}, MakeMetadata("z"));
}

TEST_F(AutoShardingTest, StartupFallback) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  // Before the stream fails, picks are queued without creating subchannels.
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  for (absl::string_view address : kAddresses) {
    EXPECT_EQ(FindSubchannel(address, args), nullptr);
  }
  // When the initial stream fails without a valid assignment, startup fallback
  // uses all resolver endpoints.
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  // The pick should trigger a connection attempt on exactly one endpoint.
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    subchannel = FindSubchannel(address, args);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // No other subchannels should have been created.
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address, args);
    if (sc != nullptr) EXPECT_EQ(sc, subchannel);
  }
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata("some_key"));
  EXPECT_THAT(address,
              ::testing::Optional(AnyOf(kAddresses[0], kAddresses[1])));
}

TEST_F(AutoShardingTest, FallbackDisabledQueuesPicksUntilAssignmentTimeout) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(),
                                    MakeChannelArgs()),
                        lb_policy()),
            absl::OkStatus());
  // Before the initial assignment timer fires, we have no result yet, so
  // picks are queued in IDLE.
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // When the initial assignment timer fires without an assignment, picks
  // fail, because fallback is disabled.
  IncrementTimeBy(Duration::Seconds(2));
  const absl::Status kTimeout = absl::UnavailableError(
      "timed out waiting for initial assignment from autosharding server");
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kTimeout);
  ExpectPickFail(
      picker.get(),
      [kTimeout](const absl::Status& status) { EXPECT_EQ(status, kTimeout); },
      /*call_attributes=*/{}, MakeMetadata("some_key"));
}

TEST_F(AutoShardingTest, StreamFailureFailsPicksWhenFallbackDisabled) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(),
                                    MakeChannelArgs()),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // When the stream fails before a valid assignment, picks fail with the
  // stream status, because fallback is disabled.
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(
      absl::UnavailableError("autosharding server unreachable"));
  const absl::Status kError = absl::UnavailableError(
      "stream to autosharding server failed: UNAVAILABLE: autosharding server "
      "unreachable");
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kError);
  ExpectPickFail(
      picker.get(),
      [kError](const absl::Status& status) { EXPECT_EQ(status, kError); },
      /*call_attributes=*/{}, MakeMetadata("some_key"));
}

TEST_F(AutoShardingTest, ResolutionNotePropagatedOnFallbackDisabled) {
  auto update =
      BuildUpdate(kAddresses, ConfigBuilder().Build(), MakeChannelArgs());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()), absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  IncrementTimeBy(Duration::Seconds(2));
  const absl::Status kTimeout = absl::UnavailableError(
      "timed out waiting for initial assignment from autosharding server "
      "(DNS resolution note)");
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kTimeout);
  ExpectPickFail(
      picker.get(),
      [kTimeout](const absl::Status& status) { EXPECT_EQ(status, kTimeout); },
      /*call_attributes=*/{}, MakeMetadata("some_key"));
}

TEST_F(AutoShardingTest, TargetChangeRecreatesClient) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetAutoshardingTarget("target1").Build(),
                      args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SendAssignment({{"m", {0}}, {"", {}}}, {EndpointName(kAddresses[0])});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status,
                  absl::UnavailableError(
                      "no endpoints assigned to this slice and fallback is "
                      "disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(factory_->num_create_channels(), 1);
  // Changing the target creates a new client, which asks the channel factory
  // for the channel again (gRFC A119).
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetAutoshardingTarget("target2").Build(),
                      args),
          lb_policy()),
      absl::OkStatus());
  EXPECT_TRUE(stream->IsOrphaned());
  auto stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_NE(stream2, stream);
  EXPECT_EQ(factory_->num_create_channels(), 2);
  // The previous assignment is still in effect until the new client reports.
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status,
                  absl::UnavailableError(
                      "no endpoints assigned to this slice and fallback is "
                      "disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  // A new assignment from the new client replaces the old one.  The new
  // client starts again at generation 0, so it accepts generation 1.
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[1])}, 1);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("z"));
}

TEST_F(AutoShardingTest, LocalitySubstitutedIntoTarget) {
  ChannelArgs args = MakeChannelArgs();
  // Without GRPC_ARG_LB_WEIGHTED_TARGET_CHILD, the first "%s" is replaced with
  // an empty string, and any subsequent "%s" is left untouched.
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(
              kAddresses,
              ConfigBuilder().SetAutoshardingTarget("svc/%s/tail/%s").Build(),
              args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(ReadRequest(stream.get()), IsInitialClientConfig("svc//tail/%s"));
  // Updating the Locality attribute recreates the client with the substituted
  // target.
  ChannelArgs args_us_east =
      args.Set(GRPC_ARG_LB_WEIGHTED_TARGET_CHILD, "us-east1");
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(
              kAddresses,
              ConfigBuilder().SetAutoshardingTarget("svc/%s/tail/%s").Build(),
              args_us_east),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  EXPECT_EQ(factory_->num_create_channels(), 2);
  EXPECT_TRUE(stream->IsOrphaned());
  stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(ReadRequest(stream.get()),
              IsInitialClientConfig("svc/us-east1/tail/%s"));
  ChannelArgs args_eu_west =
      args.Set(GRPC_ARG_LB_WEIGHTED_TARGET_CHILD, "europe-west1");
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(
              kAddresses,
              ConfigBuilder().SetAutoshardingTarget("svc/%s/tail/%s").Build(),
              args_eu_west),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  EXPECT_EQ(factory_->num_create_channels(), 3);
  EXPECT_TRUE(stream->IsOrphaned());
  stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  EXPECT_THAT(ReadRequest(stream.get()),
              IsInitialClientConfig("svc/europe-west1/tail/%s"));
}

TEST_F(AutoShardingTest, ChannelRecreatedWhenFactoryKeyChanges) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(
                BuildUpdate(
                    kAddresses,
                    ConfigBuilder().SetChannelFactoryKey("key1").Build(), args),
                lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SendAssignment({{"m", {0}}, {"", {}}}, {EndpointName(kAddresses[0])}, 1,
                 "key1");
  auto stream = WaitForStream("key1");
  ASSERT_NE(stream, nullptr);
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // A key in the slice with no endpoints fails.
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError(
                              "no endpoints assigned to this slice and "
                              "fallback is disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  EXPECT_EQ(factory_->num_create_channels(), 1);
  EXPECT_EQ(ApplyUpdate(
                BuildUpdate(
                    kAddresses,
                    ConfigBuilder().SetChannelFactoryKey("key2").Build(), args),
                lb_policy()),
            absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 2);
  EXPECT_TRUE(stream->IsOrphaned());
  EXPECT_NE(WaitForStream("key2"), nullptr);
  // The old assignment is retained until the new client reports.
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status,
                  absl::UnavailableError(
                      "no endpoints assigned to this slice and fallback is "
                      "disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
}

TEST_F(AutoShardingTest, NoChannelFactory) {
  const absl::Status kError =
      absl::UnavailableError("no channel factory in channel args");
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build()),
                        lb_policy()),
            kError);
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kError);
  ExpectPickFail(
      picker.get(),
      [kError](const absl::Status& status) { EXPECT_EQ(status, kError); },
      /*call_attributes=*/{}, MakeMetadata("some_key"));
}

TEST_F(AutoShardingTest, NoChannelFactoryFailsEvenWhenFallbackEnabled) {
  auto update =
      BuildUpdate(kAddresses, ConfigBuilder().SetEnableFallback(true).Build());
  update.resolution_note = "DNS resolution note";
  const absl::Status kError = absl::UnavailableError(
      "no channel factory in channel args (DNS resolution note)");
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()), kError);
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kError);
  ExpectPickFail(
      picker.get(),
      [kError](const absl::Status& status) { EXPECT_EQ(status, kError); },
      /*call_attributes=*/{}, MakeMetadata("some_key"));
}

TEST_F(AutoShardingTest, NoChannelFactoryKeepsExistingClient) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[0])});
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectState(GRPC_CHANNEL_IDLE);
  // The channel factory is needed only to create a new client, so an update
  // that keeps the client does not fail without one.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build()),
                        lb_policy()),
            absl::OkStatus());
  EXPECT_FALSE(stream->IsOrphaned());
  EXPECT_EQ(WaitForStream(), stream);
  // The kept client's assignment routes the pick to the assigned endpoint.
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  // The last update has no channel factory, so the subchannel uses the
  // default channel args.
  SubchannelState* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[1]), nullptr);
}

TEST_F(AutoShardingTest,
       NoChannelFactoryOnTargetChangeClosesClientAndKeepsAssignment) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SendAssignment({{"m", {0}}, {"", {}}}, {EndpointName(kAddresses[0])});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // A key in the slice with no endpoints fails.
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError(
                              "no endpoints assigned to this slice and "
                              "fallback is disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  // A target change needs a new client.  Without a channel factory, the
  // policy closes the current client and reports a bad resolver state.
  const absl::Status kError =
      absl::UnavailableError("no channel factory in channel args");
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses,
                                    ConfigBuilder()
                                        .SetEnableFallback(true)
                                        .SetAutoshardingTarget("other_target")
                                        .Build()),
                        lb_policy()),
            kError);
  EXPECT_TRUE(stream->IsOrphaned());
  picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kError);
  ExpectPickFail(
      picker.get(),
      [kError](const absl::Status& status) { EXPECT_EQ(status, kError); },
      /*call_attributes=*/{}, MakeMetadata("a"));
  // A later valid update creates a new channel and client.  The policy keeps
  // the old assignment, so picks are routed at once.
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 2);
  auto stream2 = WaitForStream();
  ASSERT_NE(stream2, nullptr);
  EXPECT_NE(stream2, stream);
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError(
                              "no endpoints assigned to this slice and "
                              "fallback is disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  // The new client reports an assignment, which replaces the old one.
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[1])});
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("z"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[1], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[0], args), nullptr);
}

TEST_F(AutoShardingTest, ChannelFactoryAddedLaterCreatesClient) {
  const absl::Status kError =
      absl::UnavailableError("no channel factory in channel args");
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build()),
                        lb_policy()),
            kError);
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kError);
  ExpectPickFail(
      picker.get(),
      [kError](const absl::Status& status) { EXPECT_EQ(status, kError); },
      /*call_attributes=*/{}, MakeMetadata("some_key"));
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 1);
  EXPECT_NE(WaitForStream(), nullptr);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // The new client reports its assignment, which routes the pick to the
  // assigned endpoint.
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[0])});
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
}

TEST_F(AutoShardingTest, SameConfigUpdateKeepsClient) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[0])});
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  ExpectState(GRPC_CHANNEL_IDLE);
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 1);
  EXPECT_FALSE(stream->IsOrphaned());
  EXPECT_EQ(WaitForStream(), stream);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
}

TEST_F(AutoShardingTest, InitialAssignmentTimeoutUsesFallbackWhenEnabled) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  // Until the client reports a result, picks are queued in IDLE even though
  // fallback is enabled.
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  for (absl::string_view address : kAddresses) {
    EXPECT_EQ(FindSubchannel(address, args), nullptr);
  }
  // When the initial assignment timer fires, picks use the fallback pool.
  IncrementTimeBy(Duration::Seconds(2));
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    subchannel = FindSubchannel(address, args);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
}

TEST_F(AutoShardingTest, EndpointUpdateRebuildsSliceMap) {
  ChannelArgs args = MakeChannelArgs();
  constexpr std::array<absl::string_view, 1> kFirstAddress = {kAddresses[0]};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kFirstAddress, ConfigBuilder().Build(), args),
                  lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // The assignment uses the second address, which the resolver does not
  // report yet.
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[1])});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError("no endpoint available"));
      },
      {}, MakeMetadata("a"));
  // When the resolver reports the second address, the slice map is rebuilt
  // from the stored assignment.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[1], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[0], args), nullptr);
}

TEST_F(AutoShardingTest, EndpointReorderAndRemovalRebuildSliceMap) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // Route all keys to kAddresses[1] (initially index 1).
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[1])});
  ExpectState(GRPC_CHANNEL_IDLE);
  // Reorder endpoints so kAddresses[1] moves to index 0.
  constexpr std::array<absl::string_view, 2> kReversedAddresses = {
      kAddresses[1], kAddresses[0]};
  EXPECT_EQ(ApplyUpdate(
                BuildUpdate(kReversedAddresses, ConfigBuilder().Build(), args),
                lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[1], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[0], args), nullptr);
  // Restore original order (kAddresses[1] at index 1), then remove the tail
  // endpoint so only kAddresses[0] (still at index 0) remains.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  // Picks still go to kAddresses[1], so they do not touch kAddresses[0].
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  EXPECT_EQ(FindSubchannel(kAddresses[0], args), nullptr);
  constexpr std::array<absl::string_view, 1> kFirstAddress = {kAddresses[0]};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kFirstAddress, ConfigBuilder().Build(), args),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError("no endpoint available"));
      },
      {}, MakeMetadata("a"));
}

TEST_F(AutoShardingTest, OldPickerDoesNotConnectToRemovedEndpoint) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // Route all keys to kAddresses[1].
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[1])});
  auto old_picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The resolver removes kAddresses[1].  The new picker has no endpoint for
  // the key.
  constexpr std::array<absl::string_view, 1> kFirstAddress = {kAddresses[0]};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kFirstAddress, ConfigBuilder().Build(), args),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError("no endpoint available"));
      },
      {}, MakeMetadata("a"));
  // The old picker still holds a ref to the removed endpoint.  A pick on it
  // requests a connection, but the removed endpoint does not create a child
  // policy, so no subchannel is created.
  ExpectPickQueued(old_picker.get(), {}, MakeMetadata("a"));
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
  EXPECT_EQ(FindSubchannel(kAddresses[0], args), nullptr);
  ExpectQueueEmpty();
}

TEST_F(AutoShardingTest, IgnoresStateUpdateFromRemovedEndpoint) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // Route all keys to kAddresses[1], and connect to it.
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[1])});
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[1], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                              MakeMetadata("a"));
  // The resolver removes kAddresses[1].  The remaining endpoint is IDLE.
  constexpr std::array<absl::string_view, 1> kFirstAddress = {kAddresses[0]};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kFirstAddress, ConfigBuilder().Build(), args),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError("no endpoint available"));
      },
      {}, MakeMetadata("a"));
  // A late state change on the removed endpoint's subchannel does not change
  // the reported state.
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  ExpectQueueEmpty();
}

TEST_F(AutoShardingTest, AssignedEndpointMissingFromResolverUsesFallback) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // The resolver does not know the assigned endpoint.  This is not a
  // server-sent empty slice, so fallback stays enabled for it.
  SendAssignment({{"", {0}}}, {"10.0.0.1:443"});
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    subchannel = FindSubchannel(address, args);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
}

TEST_F(AutoShardingTest, AssignedEndpointMissingFromResolverFails) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(),
                                    MakeChannelArgs()),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SendAssignment({{"", {0}}}, {"10.0.0.1:443"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError("no endpoint available"));
      },
      {}, MakeMetadata("a"));
}

TEST_F(AutoShardingTest, MissingKeyHeaderDropsPick) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickDrop(picker.get(), [](const absl::Status& status) {
    EXPECT_EQ(status, absl::InternalError(
                          "slice key header \"x-slice-key\" not present"));
  });
}

TEST_F(AutoShardingTest, EmptyEndpointList) {
  const std::vector<absl::string_view> kNoAddresses;
  auto update =
      BuildUpdate(kNoAddresses, ConfigBuilder().Build(), MakeChannelArgs());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()),
            absl::UnavailableError("empty address list (DNS resolution note)"));
  ExpectTransientFailureUpdate(
      absl::UnavailableError("empty address list (DNS resolution note)"));
}

TEST_F(AutoShardingTest, EmptyEndpointListKeepsClientAndAssignment) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // The server sends ["", "m") to the first endpoint and ["m", +inf) with no
  // endpoints.
  SendAssignment({{"m", {0}}, {"", {}}}, {EndpointName(kAddresses[0])});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // A key in the slice with no endpoints fails.
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError(
                              "no endpoints assigned to this slice and "
                              "fallback is disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  const std::vector<absl::string_view> kNoAddresses;
  auto update = BuildUpdate(kNoAddresses, ConfigBuilder().Build(), args);
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()),
            absl::UnavailableError("empty address list (DNS resolution note)"));
  ExpectTransientFailureUpdate(
      absl::UnavailableError("empty address list (DNS resolution note)"));
  // The empty address list does not close the client.
  EXPECT_FALSE(stream->IsOrphaned());
  // A later valid update keeps the client and its assignment, so picks are
  // routed at once.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 1);
  EXPECT_EQ(WaitForStream(), stream);
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError(
                              "no endpoints assigned to this slice and "
                              "fallback is disabled"));
      },
      /*call_attributes=*/{}, MakeMetadata("z"));
  ExpectPickQueued(picker.get(), {}, MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
}

TEST_F(AutoShardingTest, AssignmentDuringEmptyEndpointListKeepsFailing) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  const std::vector<absl::string_view> kNoAddresses;
  const absl::Status kError = absl::UnavailableError("empty address list");
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kNoAddresses, ConfigBuilder().Build(), args),
                  lb_policy()),
      kError);
  ExpectTransientFailureUpdate(kError);
  // An assignment from the kept client does not hide the resolver error.
  SendAssignment({{"", {0}}}, {EndpointName(kAddresses[0])});
  ExpectTransientFailureUpdate(kError);
  // The next valid update uses the assignment at once.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  SubchannelState* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
}

TEST_F(AutoShardingTest, EndpointsWithDuplicateHostnames) {
  // Two endpoints with the same hostname attribute.  The first one wins, and
  // the duplicate is ignored.
  ChannelArgs args = MakeChannelArgs();
  const std::vector<EndpointAddresses> endpoints = {
      MakeEndpointAddresses({kAddresses[0]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1")),
      MakeEndpointAddresses({kAddresses[1]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(endpoints,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  // The pick should trigger a connection attempt on the endpoint that won
  // (the one with index 0).
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
  auto* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                              MakeMetadata("some_key"));
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata("some_key"));
  EXPECT_THAT(address, ::testing::Optional(kAddresses[0]));
}

TEST_F(AutoShardingTest, RetainsEndpointForHostnameAcrossUpdates) {
  ChannelArgs args = MakeChannelArgs();
  // First update: a single endpoint with hostname "host1". Drive it to READY.
  const std::vector<EndpointAddresses> first_update = {MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(first_update,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                              MakeMetadata("some_key"));
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_THAT(ExpectPickComplete(picker.get(), {}, MakeMetadata("some_key")),
              ::testing::Optional(kAddresses[0]));
  // Second update: retains endpoint for "host1".
  const std::vector<EndpointAddresses> second_update = {MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(second_update,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  // Reconfiguring the reused endpoint's pick_first child makes it re-register
  // its subchannel watchers, which triggers a second READY notification on top
  // of the policy's own unconditional post-update picker refresh; both report
  // READY without an intervening CONNECTING, confirming the endpoint was never
  // actually torn down and reconnected.  Picks on both pickers still complete
  // on the retained endpoint.
  picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_THAT(ExpectPickComplete(picker.get(), {}, MakeMetadata("some_key")),
              ::testing::Optional(kAddresses[0]));
  picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_THAT(ExpectPickComplete(picker.get(), {}, MakeMetadata("some_key")),
              ::testing::Optional(kAddresses[0]));
}

TEST_F(AutoShardingTest, EmptyKeyHeaderValueIsValidKey) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata(""));
}

TEST_F(AutoShardingTest, SameAddressListedMultipleTimes) {
  ChannelArgs args = MakeChannelArgs();
  const std::array<absl::string_view, 3> kAddressesWithDup = {
      kAddresses[0], kAddresses[1], kAddresses[0]};
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddressesWithDup,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  const auto metadata = MakeMetadata("some_key");
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(), metadata);
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    subchannel = FindSubchannel(address, args);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, metadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest, MultipleAddressesPerEndpoint) {
  ChannelArgs args = MakeChannelArgs();
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  const std::array<EndpointAddresses, 2> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses)};
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kEndpoints,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  const auto metadata = MakeMetadata("some_key");
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(), metadata);
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address :
       {kEndpoint1Addresses[0], kEndpoint1Addresses[1], kEndpoint2Addresses[0],
        kEndpoint2Addresses[1]}) {
    subchannel = FindSubchannel(address, args);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                              metadata);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, metadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest,
       TriggersConnectionAttemptsInConnectingAndTransientFailureWithoutPicks) {
  ChannelArgs args = MakeChannelArgs();
  constexpr std::array<absl::string_view, 4> kFourAddresses = {
      kAddresses[0], kAddresses[1], "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  std::array<SubchannelState*, 4> subchannels;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    subchannels[i] = CreateSubchannel(kFourAddresses[i], args);
  }
  absl::flat_hash_set<SubchannelState*> failed_subchannels;
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kFourAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  auto stream = WaitForStream();
  ASSERT_NE(stream, nullptr);
  stream->MaybeSendStatusToClient(absl::UnavailableError("stream failed"));
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  SubchannelState* subchannel0 = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(subchannel0, nullptr) << "index " << i;
      subchannel0 = subchannels[i];
    }
  }
  ASSERT_NE(subchannel0, nullptr);
  subchannel0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i] != subchannel0) {
      EXPECT_FALSE(subchannels[i]->ConnectionRequested()) << "index " << i;
    }
  }
  subchannel0->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(subchannel0);
  ExpectReresolutionRequest();
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  SubchannelState* connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  ASSERT_FALSE(failed_subchannels.contains(connecting_subchannel));
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  ASSERT_FALSE(failed_subchannels.contains(connecting_subchannel));
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  subchannel0->SetConnectivityState(GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(subchannel0->ConnectionRequested());
  subchannel0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel0->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata("some_key"));
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest, ProactiveConnectionAttemptChoosesRandomIdleEndpoint) {
  constexpr std::array<absl::string_view, 4> kFourAddresses = {
      kAddresses[0], kAddresses[1], "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  // Each policy routes all keys to kFourAddresses[0].  When that endpoint
  // fails, the policy triggers a connection attempt on one of the other
  // endpoints.  Run this with many policies, and check that more than one
  // endpoint is chosen.
  constexpr int kNumPolicies = 20;
  std::set<absl::string_view> chosen_addresses;
  for (int i = 0; i < kNumPolicies; ++i) {
    if (i > 0) RecreateLbPolicy();
    // Use a different channel factory key and subchannel args for each
    // policy, so that each policy has its own stream and subchannels.
    const std::string channel_factory_key = absl::StrCat("key", i);
    ChannelArgs args = MakeChannelArgs().Set("grpc.test.policy_index", i);
    std::array<SubchannelState*, 4> subchannels;
    for (size_t j = 0; j < subchannels.size(); ++j) {
      subchannels[j] = CreateSubchannel(kFourAddresses[j], args);
    }
    EXPECT_EQ(
        ApplyUpdate(BuildUpdate(kFourAddresses,
                                ConfigBuilder()
                                    .SetChannelFactoryKey(channel_factory_key)
                                    .Build(),
                                args),
                    lb_policy()),
        absl::OkStatus());
    ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                                MakeMetadata("some_key"));
    SendAssignment({{"", {0}}}, {EndpointName(kFourAddresses[0])},
                   /*generation=*/1, channel_factory_key);
    ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                                MakeMetadata("a"));
    EXPECT_TRUE(subchannels[0]->ConnectionRequested());
    subchannels[0]->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                                MakeMetadata("a"));
    // While an endpoint is CONNECTING, the policy does not trigger another
    // connection attempt.
    for (size_t j = 1; j < subchannels.size(); ++j) {
      EXPECT_FALSE(subchannels[j]->ConnectionRequested()) << "index " << j;
    }
    subchannels[0]->SetConnectivityState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::UnavailableError("connection attempt failed"));
    ExpectReresolutionRequest();
    auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
    // The key is assigned only to the failed endpoint, and fallback is
    // disabled, so the pick fails.
    ExpectPickFail(
        picker.get(),
        [](const absl::Status& status) {
          EXPECT_EQ(status, absl::UnavailableError(
                                "UNAVAILABLE: connection attempt failed"));
        },
        {}, MakeMetadata("a"));
    // The policy triggers a connection attempt on exactly one IDLE endpoint.
    SubchannelState* chosen = nullptr;
    for (size_t j = 1; j < subchannels.size(); ++j) {
      if (subchannels[j]->ConnectionRequested()) {
        ASSERT_EQ(chosen, nullptr) << "index " << j;
        chosen = subchannels[j];
        chosen_addresses.insert(kFourAddresses[j]);
      }
    }
    ASSERT_NE(chosen, nullptr);
    chosen->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    ExpectState(GRPC_CHANNEL_CONNECTING);
  }
  EXPECT_GT(chosen_addresses.size(), 1u);
}

TEST_F(AutoShardingTest, ExitIdleInIdleTriggersOneConnectionAttempt) {
  ChannelArgs args = MakeChannelArgs();
  std::array<SubchannelState*, 2> subchannels;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    subchannels[i] = CreateSubchannel(kAddresses[i], args);
  }
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // Keys before "m" go to kAddresses[0], and the rest go to kAddresses[1].
  const std::array<std::string, 2> kKeys = {"a", "z"};
  SendAssignment({{"m", {0}}, {"", {1}}},
                 {EndpointName(kAddresses[0]), EndpointName(kAddresses[1])});
  ExpectState(GRPC_CHANNEL_IDLE);
  for (SubchannelState* subchannel : subchannels) {
    EXPECT_FALSE(subchannel->ConnectionRequested());
  }
  // In IDLE, a request to exit IDLE triggers a connection attempt on exactly
  // one endpoint.
  ExitIdle();
  size_t chosen = subchannels.size();
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(chosen, subchannels.size()) << "index " << i;
      chosen = i;
    }
  }
  ASSERT_LT(chosen, subchannels.size());
  subchannels[chosen]->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                              MakeMetadata(kKeys[chosen]));
}

TEST_F(AutoShardingTest, ExitIdleInConnectingOrReadyDoesNothing) {
  ChannelArgs args = MakeChannelArgs();
  std::array<SubchannelState*, 2> subchannels;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    subchannels[i] = CreateSubchannel(kAddresses[i], args);
  }
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("some_key"));
  // Keys before "m" go to kAddresses[0], and the rest go to kAddresses[1].
  SendAssignment({{"m", {0}}, {"", {1}}},
                 {EndpointName(kAddresses[0]), EndpointName(kAddresses[1])});
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE, absl::OkStatus(),
                              MakeMetadata("a"));
  EXPECT_TRUE(subchannels[0]->ConnectionRequested());
  subchannels[0]->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_CONNECTING, absl::OkStatus(),
                              MakeMetadata("a"));
  // In CONNECTING, a request to exit IDLE does nothing.
  ExitIdle();
  EXPECT_FALSE(subchannels[1]->ConnectionRequested());
  subchannels[0]->SetConnectivityState(GRPC_CHANNEL_READY);
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_THAT(ExpectPickComplete(picker.get(), {}, MakeMetadata("a")),
              ::testing::Optional(kAddresses[0]));
  // In READY, a request to exit IDLE does nothing, even though an endpoint
  // is IDLE.
  ExitIdle();
  EXPECT_FALSE(subchannels[1]->ConnectionRequested());
  ExpectQueueEmpty();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
