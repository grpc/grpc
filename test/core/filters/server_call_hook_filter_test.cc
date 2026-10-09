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
#include <grpc/status.h>

#include <cstring>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/server/server_call_tracer_filter.h"
#include "test/core/filters/filter_test.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace {

// The hook's verdict is a file-scope global so the C callback stays capture-free.
grpc_status_code g_verdict = GRPC_STATUS_OK;

grpc_status_code HookOnInitialMetadata(void* /*user_data*/,
                                       grpc_server_call_hook_header_lookup,
                                       void* /*lookup_context*/,
                                       void** /*call_data*/) {
  return g_verdict;
}

grpc_server_call_hook_vtable MakeHook() {
  grpc_server_call_hook_vtable v;
  std::memset(&v, 0, sizeof(v));
  v.struct_size = sizeof(v);
  v.on_initial_metadata = HookOnInitialMetadata;
  return v;
}

grpc_server_call_hook_vtable g_hook = MakeHook();

}  // namespace

class ServerCallHookFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;
};

FILTER_TEST(ServerCallHookFilterTest, AdmitsWhenNoHook) {
  grpc_server_call_hook_register(nullptr);
  ASSERT_TRUE(CreateFilterChain<ServerCallTracerFilter>(ChannelArgs()).ok());
  StartCallForFilter(NewClientMetadata());
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
}

FILTER_TEST(ServerCallHookFilterTest, AdmitsWhenHookReturnsOk) {
  g_verdict = GRPC_STATUS_OK;
  grpc_server_call_hook_register(&g_hook);
  ASSERT_TRUE(CreateFilterChain<ServerCallTracerFilter>(ChannelArgs()).ok());
  StartCallForFilter(NewClientMetadata());
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
  grpc_server_call_hook_register(nullptr);
}

// The refuse path is not reproduced here: FilterTest has no handler to orphan
// when a call is refused at initial metadata, which is why no in-tree filter
// test rejects there. It is covered end to end by an out-of-tree extension.

TEST(ServerCallHookRegistrationTest, IgnoresUndersizedVtable) {
  grpc_server_call_hook_vtable small;
  std::memset(&small, 0, sizeof(small));
  small.struct_size = 0;
  grpc_server_call_hook_register(&small);
  EXPECT_EQ(GetServerCallHook(), nullptr);
  grpc_server_call_hook_register(nullptr);
}

}  // namespace grpc_core
