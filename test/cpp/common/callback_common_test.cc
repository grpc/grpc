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

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/time.h>
#include <grpcpp/impl/completion_queue_tag.h>
#include <grpcpp/support/callback_common.h>

#include <functional>
#include <utility>

#include "src/core/util/notification.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc {
namespace internal {
namespace {

class TestCompletionQueueTag : public CompletionQueueTag {
 public:
  explicit TestCompletionQueueTag(std::function<void()> on_finalize)
      : on_finalize_(std::move(on_finalize)) {}

  bool FinalizeResult(void** /*tag*/, bool* /*status*/) override {
    if (on_finalize_) {
      on_finalize_();
    }
    return true;
  }

 private:
  std::function<void()> on_finalize_;
};

struct Holder {
  CallbackWithSuccessTag tag;
};

TEST(CallbackCommonTest,
     CallbackWithSuccessTagDestructionDuringFinalizeResult) {
  grpc_init();
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_channel* channel = grpc_lame_client_channel_create(
      "fake_target", GRPC_STATUS_UNAVAILABLE, "test");
  grpc_slice host = grpc_slice_from_static_string("foo.test.google.com");
  grpc_call* call = grpc_channel_create_call(
      channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/service/method"), &host,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_core::Notification notification;
  bool callback_ran = false;
  auto* holder = new Holder();
  TestCompletionQueueTag test_ops([&holder]() {
    // Delete the holder containing the CallbackWithSuccessTag during
    // FinalizeResult. Without the fix, accessing `this->func_` or `this->call_`
    // after FinalizeResult causes a heap-use-after-free or segmentation fault.
    delete holder;
    holder = nullptr;
  });

  holder->tag.Set(
      call,
      [&notification, &callback_ran](bool ok) {
        EXPECT_TRUE(ok);
        callback_ran = true;
        notification.Notify();
      },
      &test_ops, /*can_inline=*/false);

  // Directly invoke the completion queue callback.
  holder->tag.force_run(true);

  notification.WaitForNotification();
  EXPECT_TRUE(callback_ran);

  grpc_call_unref(call);
  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();
}

}  // namespace
}  // namespace internal
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
