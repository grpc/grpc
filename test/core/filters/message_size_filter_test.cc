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

#include "src/core/ext/filters/message_size/message_size_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {
const absl::string_view kTestPath = "/test_method";
}  // namespace

class MessageSizeFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  absl::Status InitServerWithMaxRecvSize(int max_recv_size) {
    return CreateFilterChain<ServerMessageSizeFilter>(
        ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, max_recv_size));
  }

  absl::Status InitClientWithMaxSendSize(int max_send_size) {
    return CreateFilterChain<ClientMessageSizeFilter>(
        ChannelArgs().Set(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, max_send_size));
  }

  absl::Status InitClientWithMaxRecvSize(int max_recv_size) {
    return CreateFilterChain<ClientMessageSizeFilter>(
        ChannelArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, max_recv_size));
  }

  // Make every call carry a service config with the given method config fields,
  // so we exercise the service-config path rather than channel args.
  void SetMessageSizeServiceConfig(absl::string_view fields) {
    absl::StatusOr<RefCountedPtr<ServiceConfig>> service_config =
        ServiceConfigImpl::Create(
            ChannelArgs(),
            absl::StrCat(R"({"methodConfig":[{"name":[{}],)", fields, "}]}"));
    ASSERT_TRUE(service_config.ok()) << service_config.status();
    service_config_ = std::move(*service_config);
    method_configs_ = service_config_->GetMethodParsedConfigVector(
        Slice::FromCopiedString(kTestPath).c_slice());
  }

  ClientMetadataHandle MakeClientInitialMetadata() {
    ClientMetadataHandle md = NewClientMetadata();
    md->Set(HttpPathMetadata(), Slice::FromCopiedString(kTestPath));
    return md;
  }

  void InitAfterCallArena(Arena* arena) override {
    if (service_config_ == nullptr) return;
    arena->New<ServiceConfigCallData>(arena)->SetServiceConfig(service_config_,
                                                               method_configs_);
  }

 private:
  RefCountedPtr<ServiceConfig> service_config_;
  const ServiceConfigParser::ParsedConfigVector* method_configs_ = nullptr;
};

// A message within the limit passes through and the call completes normally.
FILTER_TEST(MessageSizeFilterTest, WithinLimitPasses) {
  ASSERT_TRUE(InitServerWithMaxRecvSize(1024).ok());
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("small"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload("small"));
  EXPECT_TRUE(PullClientHalfClose());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

// The limit is inclusive: a message of exactly the limit is delivered.
FILTER_TEST(MessageSizeFilterTest, AtLimitPasses) {
  ASSERT_TRUE(InitServerWithMaxRecvSize(5).ok());
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("exact"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload("exact"));
  EXPECT_TRUE(PullClientHalfClose());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());

  WaitForAllPendingWork();
}

// The server filter enforces the receive limit on client->server messages: an
// oversized message is rejected, and the client sees RESOURCE_EXHAUSTED.
FILTER_TEST(MessageSizeFilterTest, ServerRecvExceedsLimitRejected) {
  ASSERT_TRUE(InitServerWithMaxRecvSize(4).ok());
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("much too big"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  // The filter rejected the message, so the pull fails rather than delivering.
  EXPECT_FALSE(PullClientMessage().ok());

  EXPECT_EQ(PullServerTrailingStatus().code(),
            absl::StatusCode::kResourceExhausted);

  WaitForAllPendingWork();
}

// The client filter enforces the send limit: an oversized message is rejected
// before it reaches the server.
FILTER_TEST(MessageSizeFilterTest, ClientSendExceedsLimitRejected) {
  ASSERT_TRUE(InitClientWithMaxSendSize(4).ok());
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage("much too big"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  EXPECT_FALSE(PullClientMessage().ok());

  EXPECT_EQ(PullServerTrailingStatus().code(),
            absl::StatusCode::kResourceExhausted);

  WaitForAllPendingWork();
}

// The client filter enforces the receive limit on server->client messages: an
// oversized response is rejected and the client sees RESOURCE_EXHAUSTED.
FILTER_TEST(MessageSizeFilterTest, ClientRecvExceedsLimitRejected) {
  ASSERT_TRUE(InitClientWithMaxRecvSize(4).ok());
  StartCallForFilter(NewClientMetadata());

  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage("much too big"));

  ASSERT_TRUE(PullServerInitialMetadata().ok());
  EXPECT_FALSE(PullServerMessage().ok());

  EXPECT_EQ(PullServerTrailingStatus().code(),
            absl::StatusCode::kResourceExhausted);

  WaitForAllPendingWork();
}

// The client filter also honors the send limit configured via the service
// config (maxRequestMessageBytes), not just channel args.
FILTER_TEST(MessageSizeFilterTest, ClientSendLimitFromServiceConfig) {
  ASSERT_TRUE(CreateFilterChain<ClientMessageSizeFilter>().ok());
  SetMessageSizeServiceConfig(R"("maxRequestMessageBytes": 4)");
  StartCallForFilter(MakeClientInitialMetadata());

  PushClientMessage(NewMessage("much too big"));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  EXPECT_FALSE(PullClientMessage().ok());

  EXPECT_EQ(PullServerTrailingStatus().code(),
            absl::StatusCode::kResourceExhausted);

  WaitForAllPendingWork();
}

}  // namespace grpc_core
