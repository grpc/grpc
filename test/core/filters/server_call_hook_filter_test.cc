// Copyright 2026 The gRPC Authors.
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

#include <grpc/server_call_hook.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/server/server_call_tracer_filter.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

int g_ends;
grpc_status_code g_end_status;

// Refuses a call whose x-tier is "low", with a header; a reserved key fails.
grpc_status_code OnCallStart(void*, const grpc_server_call_hook_ops* ops,
                             void* call, uint64_t* call_word,
                             const char** message, size_t* message_len) {
  const char* tier = nullptr;
  size_t len = 0;
  ops->get(call, "x-tier", 6, &tier, &len);
  ops->add(call, "x-why", 5, "load", 4);
  if (ops->add(call, "grpc-status", 11, "0", 1)) return GRPC_STATUS_INTERNAL;
  *call_word = 0xC0FFEE;
  *message = "over capacity";
  *message_len = 13;
  return absl::string_view(tier, len) == "low" ? GRPC_STATUS_UNAVAILABLE
                                               : GRPC_STATUS_OK;
}

void OnCallEnd(void*, uint64_t call_word, grpc_status_code status, int64_t) {
  g_ends += call_word == 0xC0FFEE;
  g_end_status = status;
}

const grpc_server_call_hook kHook = {sizeof(grpc_server_call_hook), nullptr,
                                     OnCallStart, OnCallEnd};
const grpc_server_call_hook kOlderHook = {sizeof(grpc_server_call_hook) - 1,
                                          nullptr, OnCallStart, OnCallEnd};

}  // namespace

class ServerCallHookFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  // Runs one call, which the hook sees end once with its call word.
  ServerMetadataHandle Call(absl::string_view tier, grpc_status_code status) {
    g_ends = 0;
    grpc_server_call_hook_register(&kHook);
    grpc_server_call_hook_register(&kOlderHook);  // ignored
    EXPECT_EQ(GetServerCallHook(), &kHook);
    EXPECT_TRUE(CreateFilterChain<ServerCallTracerFilter>(ChannelArgs()).ok());
    StartCallForFilter(NewClientMetadata({{"x-tier", tier}}));
    if (PullClientInitialMetadata().ok()) {
      PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
    }
    ValueOrFailure<ServerMetadataHandle> md = PullServerTrailingMetadata();
    WaitForAllPendingWork();
    EXPECT_EQ(g_ends, 1);
    EXPECT_EQ(g_end_status, status);
    return md.ok() ? std::move(*md) : nullptr;
  }
};

FILTER_TEST(ServerCallHookFilterTest, AdmitsAndDropsStagedHeaders) {
  ServerMetadataHandle md = Call("high", GRPC_STATUS_OK);
  ASSERT_NE(md, nullptr);
  EXPECT_THAT(*md, LacksMetadataKey("x-why"));
}

FILTER_TEST(ServerCallHookFilterTest, RefusesWithMessageAndHeader) {
  ServerMetadataHandle md = Call("low", GRPC_STATUS_UNAVAILABLE);
  ASSERT_NE(md, nullptr);
  EXPECT_THAT(*md, HasMetadataResult(absl::UnavailableError("over capacity")));
  EXPECT_THAT(*md, HasMetadataKeyValue("x-why", "load"));
}

}  // namespace grpc_core
