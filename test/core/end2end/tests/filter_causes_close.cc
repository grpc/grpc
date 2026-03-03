//
//
// Copyright 2015 gRPC authors.
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

#include <grpc/status.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/call/call_filters.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"
#include "test/core/call/batch_builder.h"
#include "test/core/end2end/end2end_tests.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

constexpr absl::string_view kFailureMode = "failure-mode";
constexpr absl::string_view kGoodMessage = "foo";
constexpr absl::string_view kBadMessage = "bar";

////////////////////////////////////////////////////////////////////////////////
// Test filter that fails on receiving a message with kBadMessage.

class TestFilterFailOnMessage
    : public ImplementChannelFilter<TestFilterFailOnMessage> {
 public:
  static const grpc_channel_filter kFilter;
  static absl::string_view TypeName() {
    return "filter_causes_close_on_message";
  }
  static absl::StatusOr<std::unique_ptr<TestFilterFailOnMessage>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<TestFilterFailOnMessage>();
  }

  class Call {
   public:
    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    ServerMetadataHandle OnClientToServerMessage(const Message& message) {
      if (message.payload()->JoinIntoString() == kGoodMessage) {
        return nullptr;
      }

      auto md = GetContext<Arena>()->MakePooled<ServerMetadata>();
      md->Set(GrpcStatusMetadata(), GRPC_STATUS_PERMISSION_DENIED);
      md->Set(GrpcMessageMetadata(),
              Slice::FromStaticString("Failure that's not preventable."));
      md->Set(HostMetadata(), Slice::FromStaticString("test-host"));
      md->Set(GrpcTarPit());
      md->Append(
          "test-failure", Slice::FromStaticString("Failing as requested."),
          [](absl::string_view, const Slice&) {
            LOG(FATAL)
                << "Appending test_failure on the server should never happen.";
          });
      md->Append(
          "test-failure-bin",
          Slice::FromStaticString("Failing as requested binary."),
          [](absl::string_view, const Slice&) {
            LOG(FATAL)
                << "Appending test_failure on the server should never happen.";
          });
      return md;
    }
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };
};

////////////////////////////////////////////////////////////////////////////////
// Test filter that fails on client initial metadata with kFailureMode.

class TestFilterFailOnClientInitialMetadata
    : public ImplementChannelFilter<TestFilterFailOnClientInitialMetadata> {
 public:
  static const grpc_channel_filter kFilter;
  static absl::string_view TypeName() {
    return "filter_causes_close_on_client_initial_metadata";
  }
  static absl::StatusOr<std::unique_ptr<TestFilterFailOnClientInitialMetadata>>
  Create(const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<TestFilterFailOnClientInitialMetadata>();
  }

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& md) {
      std::string failure_mode_buffer;
      std::optional<absl::string_view> failure_mode =
          md.GetStringValue(kFailureMode, &failure_mode_buffer);
      if (failure_mode.has_value() && *failure_mode == "true") {
        return grpc_error_set_int(
            absl::PermissionDeniedError("More failure that's not preventable."),
            StatusIntProperty::kRpcStatus, GRPC_STATUS_PERMISSION_DENIED);
      }
      return absl::OkStatus();
    }
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };
};

const NoInterceptor
    TestFilterFailOnClientInitialMetadata::Call::OnClientToServerMessage;
const NoInterceptor
    TestFilterFailOnClientInitialMetadata::Call::OnServerInitialMetadata;
const NoInterceptor
    TestFilterFailOnClientInitialMetadata::Call::OnServerTrailingMetadata;
const NoInterceptor
    TestFilterFailOnClientInitialMetadata::Call::OnClientToServerHalfClose;
const NoInterceptor
    TestFilterFailOnClientInitialMetadata::Call::OnServerToClientMessage;
const NoInterceptor TestFilterFailOnClientInitialMetadata::Call::OnFinalize;

const NoInterceptor TestFilterFailOnMessage::Call::OnClientInitialMetadata;
const NoInterceptor TestFilterFailOnMessage::Call::OnServerInitialMetadata;
const NoInterceptor TestFilterFailOnMessage::Call::OnServerTrailingMetadata;
const NoInterceptor TestFilterFailOnMessage::Call::OnClientToServerHalfClose;
const NoInterceptor TestFilterFailOnMessage::Call::OnServerToClientMessage;
const NoInterceptor TestFilterFailOnMessage::Call::OnFinalize;

const grpc_channel_filter TestFilterFailOnClientInitialMetadata::kFilter =
    MakePromiseBasedFilter<TestFilterFailOnClientInitialMetadata,
                           FilterEndpoint::kServer>();

const grpc_channel_filter TestFilterFailOnMessage::kFilter =
    MakePromiseBasedFilter<TestFilterFailOnMessage, FilterEndpoint::kServer,
                           kFilterExaminesInboundMessages>();

///////////////////////////////////////////////////////////////////////////////
// Tests

// Test to verify that the server can close the call when a filter fails.
// Also verifies that the server sends trailing metadata with the failed
// status and message to the client.
void FilterCloseOnInitialMetadata(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({{kFailureMode, "true"}})
      .SendMessage(kGoodMessage)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  test.Expect(1, true);
  test.Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_EQ(server_status.message(), "More failure that's not preventable.");
}

// Test to verify that the server can close the call when a filter fails.
// Also verifies that the cancellation is propagated through the filters and
// the metadata fields set in the filter are sent back to the client.
void FilterCloseOnMessage(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(10)).Create();

  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMetadata server_trailing_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({{kFailureMode, "false"}})
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);

  auto s = test.RequestCall(100);
  test.Expect(100, true);
  test.Step();

  IncomingMessage server_message;
  s.NewBatch(101).RecvMessage(server_message);
  c.NewBatch(2).SendMessage(kGoodMessage);
  test.Expect(2, true);
  test.Expect(101, true);
  test.Step();

  IncomingMessage server_message_2;
  s.NewBatch(102).RecvMessage(server_message_2);
  c.NewBatch(3).SendMessage(kBadMessage);
  // This behavior is caused by the fact that in case of proxy, on getting the
  // kBadMessage, the proxy (server) filter fails with the expected error
  // (invokes recv_message callback with an error) and sends expected trailing
  // metadata back to the client. But the proxy (client) sends a RST_STREAM to
  // the server to close the stream. When this happens, the server invokes
  // recv_message callback with an absl::OkStatus() and hence the RecvMessage
  // op does not fail.
  bool supports_request_proxying =
      test.test_config()->feature_mask & FEATURE_MASK_SUPPORTS_REQUEST_PROXYING;
  test.Expect(102, supports_request_proxying);
  test.Expect(3, true);
  test.Expect(1, true);
  test.Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_EQ(server_status.message(), "Failure that's not preventable.");
  EXPECT_EQ(server_status.GetTrailingMetadata("test-failure"),
            "Failing as requested.");
  EXPECT_EQ(server_status.GetTrailingMetadata("test-failure-bin"),
            "Failing as requested binary.");
  EXPECT_EQ(server_status.GetTrailingMetadata(HostMetadata::key()),
            "test-host");
}

CORE_END2END_TEST(CoreEnd2endTests, FilterCausesClose) {
  CoreConfiguration::RegisterEphemeralBuilder(
      [](CoreConfiguration::Builder* builder) {
        builder->channel_init()->RegisterFilter<TestFilterFailOnMessage>(
            GRPC_SERVER_CHANNEL);
        builder->channel_init()
            ->RegisterFilter<TestFilterFailOnClientInitialMetadata>(
                GRPC_SERVER_CHANNEL);
      });

  FilterCloseOnInitialMetadata(*this);
  if (IsPromiseFilterSendCancelMetadataEnabled()) {
    FilterCloseOnMessage(*this);
  }
}

}  // namespace
}  // namespace grpc_core
