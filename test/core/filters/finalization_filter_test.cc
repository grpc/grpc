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

#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <optional>

#include "src/core/call/call_filters.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/channelz/property_list.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/call_final_info.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

constexpr char kTestErrorString[] = "Test error string";

struct FinalizeRecord {
  // Fields to pass this struct via ChannelArgs.
  struct RawPointerChannelArgTag {};
  static absl::string_view ChannelArgName() {
    return "grpc.test.finalize_record";
  }

  bool on_finalize_called = false;
  bool has_final_info = false;
  grpc_call_final_info final_info{};
};

class TestFinalizationFilter {
 public:
  explicit TestFinalizationFilter(FinalizeRecord* const record = nullptr)
      : record_(record) {}

  TestFinalizationFilter(const TestFinalizationFilter&) = delete;
  TestFinalizationFilter& operator=(const TestFinalizationFilter&) = delete;
  TestFinalizationFilter(TestFinalizationFilter&&) = delete;
  TestFinalizationFilter& operator=(TestFinalizationFilter&&) = delete;

  static absl::StatusOr<std::unique_ptr<TestFinalizationFilter>> Create(
      const ChannelArgs& args, const FilterArgs&) {
    return std::make_unique<TestFinalizationFilter>(
        args.GetObject<FinalizeRecord>());
  }

  FinalizeRecord* record() const { return record_; }

  class Call {
   public:
    Call() = default;
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    Call(Call&&) = delete;
    Call& operator=(Call&&) = delete;

    static inline const NoInterceptor GRPC_UNUSED OnClientInitialMetadata;
    static inline const NoInterceptor GRPC_UNUSED OnServerInitialMetadata;
    static inline const NoInterceptor GRPC_UNUSED OnServerTrailingMetadata;
    static inline const NoInterceptor GRPC_UNUSED OnClientToServerMessage;
    static inline const NoInterceptor GRPC_UNUSED OnClientToServerHalfClose;
    static inline const NoInterceptor GRPC_UNUSED OnServerToClientMessage;

    void OnFinalize(const grpc_call_final_info* const final_info,
                    TestFinalizationFilter* const filter) {
      EXPECT_TRUE(HasContext<Arena>());
      EXPECT_NE(GetContext<Arena>(), nullptr);
      if (filter != nullptr && filter->record() != nullptr) {
        FinalizeRecord* const record = filter->record();
        record->on_finalize_called = true;
        record->has_final_info = (final_info != nullptr);
        if (final_info != nullptr) {
          record->final_info = *final_info;
        }
      }
    }

    channelz::PropertyList ChannelzProperties() const { return {}; }
  };

 private:
  FinalizeRecord* const record_;
};

class OneParamFinalizationFilter {
 public:
  explicit OneParamFinalizationFilter(FinalizeRecord* const record = nullptr)
      : record_(record) {}

  OneParamFinalizationFilter(const OneParamFinalizationFilter&) = delete;
  OneParamFinalizationFilter& operator=(const OneParamFinalizationFilter&) =
      delete;
  OneParamFinalizationFilter(OneParamFinalizationFilter&&) = delete;
  OneParamFinalizationFilter& operator=(OneParamFinalizationFilter&&) = delete;

  static absl::StatusOr<std::unique_ptr<OneParamFinalizationFilter>> Create(
      const ChannelArgs& args, const FilterArgs&) {
    return std::make_unique<OneParamFinalizationFilter>(
        args.GetObject<FinalizeRecord>());
  }

  FinalizeRecord* record() const { return record_; }

  class Call {
   public:
    explicit Call(OneParamFinalizationFilter* const filter)
        : record_(filter != nullptr ? filter->record() : nullptr) {}
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    Call(Call&&) = delete;
    Call& operator=(Call&&) = delete;

    static inline const NoInterceptor GRPC_UNUSED OnClientInitialMetadata;
    static inline const NoInterceptor GRPC_UNUSED OnServerInitialMetadata;
    static inline const NoInterceptor GRPC_UNUSED OnServerTrailingMetadata;
    static inline const NoInterceptor GRPC_UNUSED OnClientToServerMessage;
    static inline const NoInterceptor GRPC_UNUSED OnClientToServerHalfClose;
    static inline const NoInterceptor GRPC_UNUSED OnServerToClientMessage;

    void OnFinalize(const grpc_call_final_info* const final_info) {
      EXPECT_TRUE(HasContext<Arena>());
      EXPECT_NE(GetContext<Arena>(), nullptr);
      if (record_ != nullptr) {
        record_->on_finalize_called = true;
        record_->has_final_info = (final_info != nullptr);
        if (final_info != nullptr) {
          record_->final_info = *final_info;
        }
      }
    }

    channelz::PropertyList ChannelzProperties() const { return {}; }

   private:
    FinalizeRecord* const record_;
  };

 private:
  FinalizeRecord* const record_;
};

class FinalizationFilterTest : public FilterTest {
 public:
  using FilterTest::FilterTest;

  FinalizationFilterTest(const FinalizationFilterTest&) = delete;
  FinalizationFilterTest& operator=(const FinalizationFilterTest&) = delete;
  FinalizationFilterTest(FinalizationFilterTest&&) = delete;
  FinalizationFilterTest& operator=(FinalizationFilterTest&&) = delete;

  ~FinalizationFilterTest() override = default;

 protected:
  FinalizeRecord record_;
};

class CustomFinalInfoFilterTest : public FinalizationFilterTest {
 public:
  using FinalizationFilterTest::FinalizationFilterTest;

  CustomFinalInfoFilterTest(const CustomFinalInfoFilterTest&) = delete;
  CustomFinalInfoFilterTest& operator=(const CustomFinalInfoFilterTest&) =
      delete;
  CustomFinalInfoFilterTest(CustomFinalInfoFilterTest&&) = delete;
  CustomFinalInfoFilterTest& operator=(CustomFinalInfoFilterTest&&) = delete;

  ~CustomFinalInfoFilterTest() override = default;

 protected:
  void InitAfterCallArena(Arena* const arena) override {
    FilterTest::InitAfterCallArena(arena);
    grpc_call_final_info* const info = arena->New<grpc_call_final_info>();
    info->final_status = GRPC_STATUS_RESOURCE_EXHAUSTED;
    info->error_string = kTestErrorString;
    arena->SetContext<grpc_call_final_info>(info);
  }
};

inline bool IsPh2Enabled() {
  return IsPh2ClientEnabled() || IsPh2ServerEnabled() ||
         IsPh2ClientServerEnabled();
}

void ExpectFinalStatusAndErrorStringEq(
    const grpc_call_final_info& actual,
    const grpc_call_final_info& expected = grpc_call_final_info{}) {
  EXPECT_EQ(actual.final_status, expected.final_status);
  EXPECT_STREQ(actual.error_string, expected.error_string);
}

}  // namespace

FILTER_TEST(FinalizationFilterTest, NormalCallCompletion) {
  if (!IsPh2Enabled()) {
    GTEST_SKIP() << "PH2 experiment is not enabled";
  }
  ASSERT_TRUE(CreateFilterChain<TestFinalizationFilter>(
                  ChannelArgs().SetObject(&record_))
                  .ok());
  StartCallForFilter(NewClientMetadata());

  PushClientHalfClose();
  const ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata();
  ASSERT_TRUE(client_initial_metadata.ok());

  PushServerInitialMetadata(NewServerMetadata());
  const ValueOrFailure<std::optional<ServerMetadataHandle>>
      server_initial_metadata = PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  const ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  CloseCallHandles();
  WaitForAllPendingWork();

  EXPECT_TRUE(record_.on_finalize_called);
  EXPECT_TRUE(record_.has_final_info);
  ExpectFinalStatusAndErrorStringEq(record_.final_info);
}

FILTER_TEST(FinalizationFilterTest, EarlyCancellation) {
  if (!IsPh2Enabled()) {
    GTEST_SKIP() << "PH2 experiment is not enabled";
  }
  ASSERT_TRUE(CreateFilterChain<TestFinalizationFilter>(
                  ChannelArgs().SetObject(&record_))
                  .ok());
  StartCallForFilter(NewClientMetadata());

  CancelCall();
  CloseCallHandles();
  WaitForAllPendingWork();

  EXPECT_TRUE(record_.on_finalize_called);
  EXPECT_TRUE(record_.has_final_info);
  ExpectFinalStatusAndErrorStringEq(record_.final_info);
}

FILTER_TEST(FinalizationFilterTest, MidStreamCancellation) {
  if (!IsPh2Enabled()) {
    GTEST_SKIP() << "PH2 experiment is not enabled";
  }
  ASSERT_TRUE(CreateFilterChain<TestFinalizationFilter>(
                  ChannelArgs().SetObject(&record_))
                  .ok());
  StartCallForFilter(NewClientMetadata());

  const ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata();
  ASSERT_TRUE(client_initial_metadata.ok());
  PushServerInitialMetadata(NewServerMetadata());
  const ValueOrFailure<std::optional<ServerMetadataHandle>>
      server_initial_metadata = PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());

  CancelCall();
  CloseCallHandles();
  WaitForAllPendingWork();

  EXPECT_TRUE(record_.on_finalize_called);
  EXPECT_TRUE(record_.has_final_info);
  ExpectFinalStatusAndErrorStringEq(record_.final_info);
}

FILTER_TEST(FinalizationFilterTest, UnstartedCallSafelyBypassesFinalize) {
  if (!IsPh2Enabled()) {
    GTEST_SKIP() << "PH2 experiment is not enabled";
  }
  TestFinalizationFilter filter(&record_);
  CallFilters::StackBuilder builder;
  builder.Add(&filter);
  {
    CallInitiatorAndHandler call = MakeCall(NewClientMetadata());
    call.handler.AddCallStack(builder.Build());
  }
  WaitForAllPendingWork();

  EXPECT_FALSE(record_.on_finalize_called);
}

FILTER_TEST(FinalizationFilterTest, OneParamOnFinalizeHook) {
  if (!IsPh2Enabled()) {
    GTEST_SKIP() << "PH2 experiment is not enabled";
  }
  ASSERT_TRUE(CreateFilterChain<OneParamFinalizationFilter>(
                  ChannelArgs().SetObject(&record_))
                  .ok());
  StartCallForFilter(NewClientMetadata());

  PushClientHalfClose();
  const ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata();
  ASSERT_TRUE(client_initial_metadata.ok());

  PushServerInitialMetadata(NewServerMetadata());
  const ValueOrFailure<std::optional<ServerMetadataHandle>>
      server_initial_metadata = PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  const ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata, HasMetadataResult(absl::OkStatus()));

  CloseCallHandles();
  WaitForAllPendingWork();

  EXPECT_TRUE(record_.on_finalize_called);
  EXPECT_TRUE(record_.has_final_info);
  ExpectFinalStatusAndErrorStringEq(record_.final_info);
}

FILTER_TEST(CustomFinalInfoFilterTest, UsesArenaContextFinalInfo) {
  if (!IsPh2Enabled()) {
    GTEST_SKIP() << "PH2 experiment is not enabled";
  }
  ASSERT_TRUE(CreateFilterChain<TestFinalizationFilter>(
                  ChannelArgs().SetObject(&record_))
                  .ok());
  StartCallForFilter(NewClientMetadata());

  PushClientHalfClose();
  const ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata();
  ASSERT_TRUE(client_initial_metadata.ok());

  PushServerInitialMetadata(NewServerMetadata());
  const ValueOrFailure<std::optional<ServerMetadataHandle>>
      server_initial_metadata = PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  const ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());

  CloseCallHandles();
  WaitForAllPendingWork();

  EXPECT_TRUE(record_.on_finalize_called);
  EXPECT_TRUE(record_.has_final_info);
  grpc_call_final_info expected{};
  expected.final_status = GRPC_STATUS_RESOURCE_EXHAUSTED;
  expected.error_string = kTestErrorString;
  ExpectFinalStatusAndErrorStringEq(record_.final_info, expected);
}

}  // namespace grpc_core
