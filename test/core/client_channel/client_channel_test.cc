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

#include <memory>

#include "gtest/gtest.h"

#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

namespace {
const absl::string_view kTestTarget = "test_target";
const absl::string_view kTestPath = "/test_method";
}  // namespace

class ClientChannelTest : public YodelTest {
 public:
 protected:
  using YodelTest::YodelTest;

  ClientChannel& InitChannel(const ChannelArgs& args) {
    auto channel =
        ClientChannel::Create(std::string(kTestTarget), CompleteArgs(args));
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

 private:
  class TestClientChannelFactory final : public ClientChannelFactory {
   public:
    RefCountedPtr<Subchannel> CreateSubchannel(
        const grpc_resolved_address& address, const ChannelArgs& args) {
      Crash("unimplemented");
    }
  };

  class TestCallDestination final : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      Crash("unimplemented");
    }

    void Orphaned() override {}
  };

  class TestCallDestinationFactory final
      : public ClientChannel::CallDestinationFactory {
   public:
    TestCallDestinationFactory(ClientChannelTest* test) : test_(test) {}

    virtual RefCountedPtr<UnstartedCallDestination> CreateCallDestination(
        ClientChannel::PickerObservable picker) {
      CHECK(!test_->picker_.has_value());
      test_->picker_ = std::move(picker);
      return test_->call_destination_;
    }

   private:
    ClientChannelTest* const test_;
  };

  ChannelArgs CompleteArgs(const ChannelArgs& args) {
    return args.SetObject(&call_destination_factory_)
        .SetObject(&client_channel_factory_)
        .SetObject(ResourceQuota::Default())
        .SetObject(
            std::static_pointer_cast<
                grpc_event_engine::experimental::EventEngine>(event_engine()));
  }

  OrphanablePtr<ClientChannel> channel_;
  absl::optional<ClientChannel::PickerObservable> picker_;
  TestCallDestinationFactory call_destination_factory_{this};
  TestClientChannelFactory client_channel_factory_;
  RefCountedPtr<TestCallDestination> call_destination_ =
      MakeRefCounted<TestCallDestination>();
};

#define CLIENT_CHANNEL_TEST(name) YODEL_TEST(ClientChannelTest, name)

CLIENT_CHANNEL_TEST(NoOp) { InitChannel(ChannelArgs()); }

CLIENT_CHANNEL_TEST(CreateCall) {
  auto& channel = InitChannel(ChannelArgs());
  channel.CreateCall(MakeClientInitialMetadata());
}

}  // namespace grpc_core
