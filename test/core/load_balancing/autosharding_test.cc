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
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/client_channel/channel_factory.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/json/json.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

// A fake WatchShardingAssignment stream.
class FakeAutoshardingStream final : public XdsTransport::StreamingCall {
 public:
  explicit FakeAutoshardingStream(
      std::unique_ptr<StreamingCall::EventHandler> event_handler)
      : event_handler_(std::move(event_handler)) {}

  ~FakeAutoshardingStream() override = default;

  // Make Ref() public so that the server can keep a reference.
  using StreamingCall::Ref;

  void SendMessage(std::string payload, bool /*send_half_close*/) override {
    sent_messages_.push_back(std::move(payload));
    event_handler_->OnRequestSent(true);
  }
  void StartRecvMessage() override {}
  void SendHalfClose() override {}
  void Orphan() override { Unref(); }

  void SendMessageToClient(absl::string_view payload) {
    event_handler_->OnRecvMessage(payload);
  }

 private:
  std::unique_ptr<EventHandler> event_handler_;
  std::vector<std::string> sent_messages_;
};

// A fake transport for the autosharding service.
class FakeAutoshardingServer final : public XdsTransport {
 public:
  FakeAutoshardingServer() : XdsTransport("FakeAutoshardingServer") {}

  void StartConnectivityFailureWatch(
      RefCountedPtr<ConnectivityFailureWatcher> /*watcher*/) override {}
  void StopConnectivityFailureWatch(
      const RefCountedPtr<ConnectivityFailureWatcher>& /*watcher*/) override {}

  OrphanablePtr<StreamingCall> CreateStreamingCall(
      const char* /*method*/,
      std::unique_ptr<StreamingCall::EventHandler> event_handler,
      CallOptions /*options*/) override {
    ++num_streams_;
    OrphanablePtr<FakeAutoshardingStream> call =
        MakeOrphanable<FakeAutoshardingStream>(std::move(event_handler));
    stream_ = call->Ref().TakeAsSubclass<FakeAutoshardingStream>();
    return call;
  }

  void Orphaned() override {}
  void ResetBackoff() override {}

  void SendMessageToClient(absl::string_view payload) {
    if (stream_ != nullptr) stream_->SendMessageToClient(payload);
  }
  bool has_stream() const { return stream_ != nullptr; }
  int num_streams() const { return num_streams_; }
  void ClearStream() { stream_.reset(); }

 private:
  RefCountedPtr<FakeAutoshardingStream> stream_;
  int num_streams_ = 0;
};

// A ChannelFactory that hands out the fake autosharding server.
class FakeAutoshardingChannelFactory final
    : public experimental::ChannelFactory {
 public:
  explicit FakeAutoshardingChannelFactory(RefCountedPtr<XdsTransport> transport)
      : transport_(std::move(transport)) {}

  std::unique_ptr<ChannelHandle> CreateChannel(
      absl::string_view /*key*/) override {
    ++num_create_channels_;
    return std::make_unique<ChannelHandleImpl>(transport_);
  }

  int num_create_channels() const { return num_create_channels_; }

 private:
  RefCountedPtr<XdsTransport> transport_;
  int num_create_channels_ = 0;
};

// Builds an AssignmentChunk that assigns the given slices to the given
// endpoints.  Each slice is a (end_key, endpoint_indices) pair; an empty
// end_key means +infinity, and the start key of each slice is the end key of
// the previous slice (the first slice starts at the empty key).
std::string MakeChunkMessage(
    const std::vector<std::pair<std::string, std::vector<int32_t>>>& slices,
    const std::vector<std::string>& endpoint_names) {
  upb::Arena arena;
  auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_new(
          arena.ptr());
  auto* chunk =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_mutable_chunk(
          response, arena.ptr());
  // upb stores a pointer to the key data, so the strings must outlive the
  // serialization call.  A deque is used so that the addresses are stable.
  std::deque<std::string> start_keys;
  for (const auto& [end_key, endpoint_indices] : slices) {
    start_keys.push_back(start_keys.empty()
                             ? std::string()
                             : slices[start_keys.size() - 1].first);
    auto* slice_assignment =
        google_cloud_autosharding_v1_AssignmentChunk_add_slice_assignments(
            chunk, arena.ptr());
    auto* slice = google_cloud_autosharding_v1_SliceAssignment_mutable_slice(
        slice_assignment, arena.ptr());
    google_cloud_autosharding_v1_Slice_set_start_key(
        slice, StdStringToUpbString(start_keys.back()));
    if (!end_key.empty()) {
      google_cloud_autosharding_v1_Slice_set_end_key(
          slice, StdStringToUpbString(end_key));
    }
    for (int32_t index : endpoint_indices) {
      auto* endpoint =
          google_cloud_autosharding_v1_SliceAssignment_add_endpoints(
              slice_assignment, arena.ptr());
      google_cloud_autosharding_v1_PerSliceEndpointState_set_endpoint_index(
          endpoint, index);
    }
  }
  for (const auto& name : endpoint_names) {
    auto* endpoint = google_cloud_autosharding_v1_AssignmentChunk_add_endpoints(
        chunk, arena.ptr());
    google_cloud_autosharding_v1_EndpointState_set_endpoint(
        endpoint, StdStringToUpbString(name));
  }
  size_t length;
  char* serialized =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_serialize(
          response, arena.ptr(), &length);
  return std::string(serialized, length);
}

std::string MakeMetadataMessage(int64_t generation) {
  upb::Arena arena;
  auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_new(
          arena.ptr());
  auto* metadata =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_mutable_metadata(
          response, arena.ptr());
  google_cloud_autosharding_v1_AssignmentMetadata_set_generation(metadata,
                                                                 generation);
  size_t length;
  char* serialized =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_serialize(
          response, arena.ptr(), &length);
  return std::string(serialized, length);
}

class AutoShardingTest : public LoadBalancingPolicyTest {
 protected:
  static constexpr absl::string_view kKeyHeaderName = "x-slice-key";
  static constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"};

  class ConfigBuilder {
   public:
    ConfigBuilder() {
      SetChannelFactoryKey("sharding_service");
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

  static std::map<std::string, std::string> MakeMetadata(
      std::string key = "some_key") {
    return {{std::string(kKeyHeaderName), std::move(key)}};
  }

  // Returns channel args containing a channel factory that produces the fake
  // autosharding server.
  ChannelArgs MakeChannelArgs() {
    server_ = MakeRefCounted<FakeAutoshardingServer>();
    factory_ = std::make_shared<FakeAutoshardingChannelFactory>(server_);
    std::shared_ptr<experimental::ChannelFactory> factory = factory_;
    return ChannelArgs().SetObject(std::move(factory));
  }

  // Requests a pick on picker and expects a Drop result.
  void ExpectPickDrop(LoadBalancingPolicy::SubchannelPicker* picker,
                      std::function<void(const absl::Status&)> check_status,
                      const std::map<std::string, std::string>& metadata = {}) {
    auto pick_result = DoPick(picker, /*call_attributes=*/{}, metadata);
    auto* drop =
        std::get_if<LoadBalancingPolicy::PickResult::Drop>(&pick_result.result);
    ASSERT_NE(drop, nullptr) << PickResultString(pick_result);
    check_status(drop->status);
  }

  // Sends a complete assignment to the autosharding client.
  void SendAssignment(
      const std::vector<std::pair<std::string, std::vector<int32_t>>>& slices,
      const std::vector<std::string>& endpoint_names, int64_t generation = 1) {
    ASSERT_NE(server_, nullptr);
    ASSERT_TRUE(server_->has_stream()) << "client did not create a stream";
    server_->SendMessageToClient(MakeChunkMessage(slices, endpoint_names));
    server_->SendMessageToClient(MakeMetadataMessage(generation));
    WaitForWorkSerializerToFlush();
  }

  AutoShardingTest() : LoadBalancingPolicyTest("autosharding_experimental") {}

  void TearDown() override {
    if (server_ != nullptr) {
      server_->ClearStream();
      server_.reset();
    }
    factory_.reset();
    LoadBalancingPolicyTest::TearDown();
  }

  RefCountedPtr<FakeAutoshardingServer> server_;
  std::shared_ptr<FakeAutoshardingChannelFactory> factory_;
};

TEST_F(AutoShardingTest, ServerSentEmptySliceDisablesFallback) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  // Until the client reports a result, picks are queued.
  ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  // Server sends ["", "m") to the first endpoint and ["m", +inf) with no
  // endpoints.
  SendAssignment({{"m", {0}}, {"", {}}}, {"127.0.0.1:441"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // A key in the server-sent-empty slice is dropped even though fallback is
  // enabled and the resolver has endpoints.
  ExpectPickDrop(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
        EXPECT_THAT(status.message(),
                    ::testing::HasSubstr("fallback is disabled"));
      },
      MakeMetadata("z"));
  // A key in the first slice triggers a connection attempt on the endpoint
  // assigned to it, and picks complete to that endpoint once it is READY.
  ExpectPickQueued(picker.get(), {}, MakeMetadata("a"));
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = FindSubchannel(kAddresses[0], args);
  ASSERT_NE(subchannel, nullptr);
  // The other endpoint is not assigned to any slice, so it is not used.
  EXPECT_EQ(FindSubchannel(kAddresses[1], args), nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata("a"));
  ASSERT_TRUE(address.has_value());
  EXPECT_EQ(*address, kAddresses[0]);
}

TEST_F(AutoShardingTest, GapUsesFallbackPool) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build(),
                              MakeChannelArgs()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  // The assignment covers only ["", "m"); the rest of the keyspace is a gap.
  SendAssignment({{"m", {0}}}, {"127.0.0.1:441"});
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  // A key in the gap uses the fallback pool, so it queues a connection
  // attempt instead of failing.
  ExpectPickQueued(picker.get(), {}, MakeMetadata("z"));
}

TEST_F(AutoShardingTest, GapFailsWhenFallbackDisabled) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(),
                                    MakeChannelArgs()),
                        lb_policy()),
            absl::OkStatus());
  // Until the client reports a result, picks are queued.
  ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  // The assignment covers only ["", "m"); the rest of the keyspace is a gap.
  SendAssignment({{"m", {0}}}, {"127.0.0.1:441"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
        EXPECT_THAT(status.message(),
                    ::testing::HasSubstr("no endpoint available"));
      },
      {}, MakeMetadata("z"));
}

TEST_F(AutoShardingTest, StartupFallback) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The pick should trigger a connection attempt on exactly one endpoint.
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    subchannel = FindSubchannel(address);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // No other subchannels should have been created.
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr) EXPECT_EQ(sc, subchannel);
  }
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata());
  EXPECT_THAT(address, ::testing::AnyOf(kAddresses[0], kAddresses[1]));
}

TEST_F(AutoShardingTest, FallbackDisabledQueuesPicksUntilAssignmentTimeout) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(),
                                    MakeChannelArgs()),
                        lb_policy()),
            absl::OkStatus());
  // Before the initial assignment timer fires, we have no result yet, so
  // picks are queued.
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  // When the initial assignment timer fires without an assignment, picks
  // fail, because fallback is disabled.
  IncrementTimeBy(Duration::Seconds(2));
  const absl::Status kTimeout = absl::UnavailableError(
      "timed out waiting for initial assignment from autosharding server");
  picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kTimeout);
  ExpectPickFail(
      picker.get(),
      [kTimeout](const absl::Status& status) { EXPECT_EQ(status, kTimeout); },
      /*call_attributes=*/{}, MakeMetadata());
}

TEST_F(AutoShardingTest, ResolutionNotePropagatedOnFallbackDisabled) {
  auto update =
      BuildUpdate(kAddresses, ConfigBuilder().Build(), MakeChannelArgs());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()), absl::OkStatus());
  // Before the initial assignment timer fires, we have no result yet, so
  // picks are queued.
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  // When the initial assignment timer fires without an assignment, picks
  // fail with a status that includes the resolution note.
  IncrementTimeBy(Duration::Seconds(2));
  const absl::Status kTimeout = absl::UnavailableError(
      "timed out waiting for initial assignment from autosharding server "
      "(DNS resolution note)");
  picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kTimeout);
  ExpectPickFail(
      picker.get(),
      [kTimeout](const absl::Status& status) { EXPECT_EQ(status, kTimeout); },
      /*call_attributes=*/{}, MakeMetadata());
}

TEST_F(AutoShardingTest, ChannelReusedWhenOnlyTargetChanges) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetAutoshardingTarget("target1").Build(),
                      args),
          lb_policy()),
      absl::OkStatus());
  ExpectState(GRPC_CHANNEL_CONNECTING);
  SendAssignment({{"m", {0}}, {"", {}}}, {"127.0.0.1:441"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  EXPECT_EQ(server_->num_streams(), 1);
  EXPECT_EQ(factory_->num_create_channels(), 1);
  // Changing the target creates a new client, but reuses the channel because
  // the channel factory key did not change (gRFC A119).
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetAutoshardingTarget("target2").Build(),
                      args),
          lb_policy()),
      absl::OkStatus());
  EXPECT_EQ(server_->num_streams(), 2);
  EXPECT_EQ(factory_->num_create_channels(), 1);
  // The previous assignment is still in effect until the new client reports.
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickDrop(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_THAT(status.message(),
                    ::testing::HasSubstr("fallback is disabled"));
      },
      MakeMetadata("z"));
  // A new assignment from the new client replaces the old one.  The new
  // client starts again at generation 0, so it accepts generation 1.
  SendAssignment({{"", {0}}}, {"127.0.0.1:442"}, 1);
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata("z"));
}

TEST_F(AutoShardingTest, ChannelRecreatedWhenFactoryKeyChanges) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(
                BuildUpdate(
                    kAddresses,
                    ConfigBuilder().SetChannelFactoryKey("key1").Build(), args),
                lb_policy()),
            absl::OkStatus());
  ExpectState(GRPC_CHANNEL_CONNECTING);
  SendAssignment({{"m", {0}}, {"", {}}}, {"127.0.0.1:441"});
  ExpectState(GRPC_CHANNEL_IDLE);
  EXPECT_EQ(factory_->num_create_channels(), 1);
  EXPECT_EQ(ApplyUpdate(
                BuildUpdate(
                    kAddresses,
                    ConfigBuilder().SetChannelFactoryKey("key2").Build(), args),
                lb_policy()),
            absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 2);
  EXPECT_EQ(server_->num_streams(), 2);
  // The old assignment is retained until the new client reports.
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickDrop(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_THAT(status.message(),
                    ::testing::HasSubstr("fallback is disabled"));
      },
      MakeMetadata("z"));
}

TEST_F(AutoShardingTest, NoChannelFactory) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build()),
                        lb_policy()),
            absl::OkStatus());
  const absl::Status kError =
      absl::UnavailableError("no channel factory in channel args");
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE, kError);
  ExpectPickFail(
      picker.get(),
      [kError](const absl::Status& status) { EXPECT_EQ(status, kError); },
      /*call_attributes=*/{}, MakeMetadata());
}

TEST_F(AutoShardingTest, ChannelFactoryAddedLaterCreatesClient) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build()),
                        lb_policy()),
            absl::OkStatus());
  ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE,
              absl::UnavailableError("no channel factory in channel args"));
  // The same config, now with a channel factory, creates the client.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(),
                                    MakeChannelArgs()),
                        lb_policy()),
            absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 1);
  EXPECT_EQ(server_->num_streams(), 1);
  // The transport error is dropped, so picks are queued until the new client
  // reports.
  auto queued_picker = ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  ExpectPickQueued(queued_picker.get(), {}, MakeMetadata());
  SendAssignment({{"", {0}}}, {"127.0.0.1:441"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
}

TEST_F(AutoShardingTest, SameConfigUpdateKeepsClient) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  SendAssignment({{"", {0}}}, {"127.0.0.1:441"});
  ExpectState(GRPC_CHANNEL_IDLE);
  // A resolver update with the same config and channel args does not create
  // a new client or a new channel.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  EXPECT_EQ(factory_->num_create_channels(), 1);
  EXPECT_EQ(server_->num_streams(), 1);
  // The assignment is still in effect.
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata("a"));
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
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
  // Until the client reports a result, picks are queued even though fallback
  // is enabled.
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  for (absl::string_view address : kAddresses) {
    EXPECT_EQ(FindSubchannel(address, args), nullptr);
  }
  // When the initial assignment timer fires, picks use the fallback pool.
  IncrementTimeBy(Duration::Seconds(2));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
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
  ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  // The assignment uses the second address, which the resolver does not
  // report yet.
  SendAssignment({{"", {0}}}, {"127.0.0.1:442"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
        EXPECT_THAT(status.message(),
                    ::testing::HasSubstr("no endpoint available"));
      },
      {}, MakeMetadata("a"));
  // When the resolver reports the second address, the slice map is rebuilt
  // from the stored assignment.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build(), args),
                        lb_policy()),
            absl::OkStatus());
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata("a"));
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = FindSubchannel(kAddresses[1], args);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  EXPECT_EQ(FindSubchannel(kAddresses[0], args), nullptr);
}

TEST_F(AutoShardingTest, AssignedEndpointMissingFromResolverUsesFallback) {
  ChannelArgs args = MakeChannelArgs();
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses,
                      ConfigBuilder().SetEnableFallback(true).Build(), args),
          lb_policy()),
      absl::OkStatus());
  ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  // The resolver does not know the assigned endpoint.  This is not a
  // server-sent empty slice, so fallback stays enabled for it.
  SendAssignment({{"", {0}}}, {"10.0.0.1:443"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata("a"));
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
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
  ExpectState(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  SendAssignment({{"", {0}}}, {"10.0.0.1:443"});
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
        EXPECT_THAT(status.message(),
                    ::testing::HasSubstr("no endpoint available"));
      },
      {}, MakeMetadata("a"));
}

TEST_F(AutoShardingTest, NoChannelFactoryFallsBackWhenEnabled) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE, absl::OkStatus());
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
}

TEST_F(AutoShardingTest, MissingKeyHeaderFailsPick) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(picker.get(), [](const absl::Status& status) {
    EXPECT_EQ(status, absl::InternalError(
                          "slice key header \"x-slice-key\" not present"));
  });
}

TEST_F(AutoShardingTest, EmptyEndpointList) {
  const std::vector<absl::string_view> kNoAddresses;
  auto update = BuildUpdate(kNoAddresses, ConfigBuilder().Build());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()),
            absl::UnavailableError("empty address list: DNS resolution note"));
  ExpectTransientFailureUpdate(
      absl::UnavailableError("empty address list: DNS resolution note"));
}

TEST_F(AutoShardingTest, AssignmentWithEmptyEndpointListKeepsError) {
  const std::vector<absl::string_view> kNoAddresses;
  auto update =
      BuildUpdate(kNoAddresses, ConfigBuilder().Build(), MakeChannelArgs());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()),
            absl::UnavailableError("empty address list: DNS resolution note"));
  ExpectTransientFailureUpdate(
      absl::UnavailableError("empty address list: DNS resolution note"));
  // An assignment does not replace the empty address list error.
  SendAssignment({{"", {0}}}, {"127.0.0.1:441"});
  ExpectQueueEmpty();
}

TEST_F(AutoShardingTest, EndpointsWithDuplicateHostnames) {
  // Two endpoints with the same hostname attribute.  The first one wins, and
  // the duplicate is ignored.
  const std::vector<EndpointAddresses> endpoints = {
      MakeEndpointAddresses({kAddresses[0]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1")),
      MakeEndpointAddresses({kAddresses[1]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(endpoints,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The pick should trigger a connection attempt on the endpoint that won
  // (the one with index 0).
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  EXPECT_EQ(FindSubchannel(kAddresses[1]), nullptr);
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata());
  EXPECT_EQ(address, kAddresses[0]);
}

TEST_F(AutoShardingTest, RetainsEndpointForHostnameAcrossUpdates) {
  // First update: a single endpoint with hostname "host1". Drive it to READY.
  const std::vector<EndpointAddresses> first_update = {MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(first_update,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  // Second update: retains endpoint for "host1".
  const std::vector<EndpointAddresses> second_update = {MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(second_update,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  // Reconfiguring the reused endpoint's pick_first child makes it re-register
  // its subchannel watchers, which triggers a second READY notification on top
  // of the policy's own unconditional post-update picker refresh; both report
  // READY without an intervening CONNECTING, confirming the endpoint was never
  // actually torn down and reconnected.
  ExpectState(GRPC_CHANNEL_READY);
  ExpectState(GRPC_CHANNEL_READY);
}

TEST_F(AutoShardingTest, EmptyKeyHeaderValueIsValidKey) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata(""));
}

TEST_F(AutoShardingTest, SameAddressListedMultipleTimes) {
  const std::array<absl::string_view, 3> kAddressesWithDup = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:441"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddressesWithDup,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  const auto metadata = MakeMetadata();
  ExpectPickQueued(picker.get(), {}, metadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address :
       {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}) {
    subchannel = FindSubchannel(address);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, metadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest, MultipleAddressesPerEndpoint) {
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  const std::array<EndpointAddresses, 2> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses)};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kEndpoints,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  const auto metadata = MakeMetadata();
  ExpectPickQueued(picker.get(), {}, metadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address :
       {"ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
        "ipv4:127.0.0.1:446"}) {
    subchannel = FindSubchannel(address);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {}, metadata);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, metadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest,
       TriggersConnectionAttemptsInConnectingAndTransientFailureWithoutPicks) {
  const std::array<absl::string_view, 4> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444"};
  std::array<SubchannelState*, 4> subchannels;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    subchannels[i] = CreateSubchannel(kAddresses[i]);
  }
  absl::flat_hash_set<SubchannelState*> failed_subchannels;
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel0 = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(subchannel0, nullptr) << "index " << i;
      subchannel0 = subchannels[i];
    }
  }
  ASSERT_NE(subchannel0, nullptr);
  subchannel0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
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
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  EXPECT_TRUE(subchannel0->ConnectionRequested());
  subchannel0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel0->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata());
  ASSERT_TRUE(address.has_value());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
