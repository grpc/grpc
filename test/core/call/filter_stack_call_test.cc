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

#include "src/core/lib/surface/filter_stack_call.h"

#include <grpc/grpc.h>

#include "src/core/config/config_vars.h"
#include "src/core/lib/surface/call.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(FilterStackCallTest, FinalErrorCapturedOnCancel) {
  ConfigVars::Overrides overrides;
  overrides.experiments = "-call_v3";
  ConfigVars::SetOverrides(overrides);
  grpc_init();

  grpc_channel* channel = grpc_lame_client_channel_create(
      "localhost:1234", GRPC_STATUS_UNAVAILABLE, "lame channel");
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_call* call =
      grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_call_cancel_with_status(call, GRPC_STATUS_UNAVAILABLE, "test error",
                               nullptr);
  auto final_error = grpc_call_get_final_error(call);
  EXPECT_TRUE(final_error.has_value());
  if (final_error.has_value()) {
    EXPECT_EQ(final_error.value().code(), absl::StatusCode::kUnavailable);
  }

  grpc_call_unref(call);
  grpc_completion_queue_destroy(cq);
  grpc_channel_destroy(channel);
  grpc_shutdown();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
