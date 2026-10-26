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
#include <grpcpp/support/global_callback_hook.h>

#include <new>

#include "src/core/util/notification.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"

namespace grpc {
namespace internal {
namespace {

class TestCompletionQueueTag : public CompletionQueueTag {
 public:
  bool FinalizeResult(void** /*tag*/, bool* /*status*/) override {
    return true;
  }
};

struct Holder {
  int magic = 0xCAFEBABE;
  CallbackWithSuccessTag tag;
  ~Holder() { magic = 0; }
};

class ArenaCheckingCallbackHook final : public GlobalCallbackHook {
 public:
  explicit ArenaCheckingCallbackHook(grpc_core::Notification* done)
      : done_(done) {}

  void RunCallback(grpc_call* call,
                   absl::FunctionRef<void()> callback) override {
    ASSERT_NE(call, nullptr);
    int* pre_marker =
        static_cast<int*>(grpc_call_arena_alloc(call, sizeof(int)));
    ASSERT_NE(pre_marker, nullptr);
    *pre_marker = 0x12345678;

    CatchingCallback(callback);

    // Verify that `call` and its arena remain valid after `callback()` has
    // destroyed the owning object and cleared the tag.
    EXPECT_EQ(*pre_marker, 0x12345678);
    int* post_marker =
        static_cast<int*>(grpc_call_arena_alloc(call, sizeof(int)));
    ASSERT_NE(post_marker, nullptr);
    *post_marker = 0x87654321;
    done_->Notify();
  }

 private:
  grpc_core::Notification* done_;
};

TEST(CallbackCommonTest,
     CallbackWithSuccessTagDestructionInsideCallbackKeepsCallAliveForHook) {
  grpc_init();
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_channel* channel = grpc_lame_client_channel_create(
      "fake_target", GRPC_STATUS_UNAVAILABLE, "test");
  grpc_slice host = grpc_slice_from_static_string("foo.test.google.com");
  grpc_call* call = grpc_channel_create_call(
      channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/service/method"), &host,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_core::Notification hook_done;
  SetGlobalCallbackHook(new ArenaCheckingCallbackHook(&hook_done));

  bool callback_ran = false;
  auto* holder = new (grpc_call_arena_alloc(call, sizeof(Holder))) Holder();
  TestCompletionQueueTag test_ops;

  // Models ClientCallback*Impl / ServerCallback*Impl where the owner is
  // allocated on the call arena, captured by the callback, and destroyed
  // inside the callback (calling ~CallbackWithSuccessTag() -> Clear()).
  holder->tag.Set(
      call,
      [holder, &callback_ran](bool ok) {
        EXPECT_TRUE(ok);
        EXPECT_EQ(holder->magic, 0xCAFEBABE);
        callback_ran = true;
        holder->~Holder();
      },
      &test_ops, /*can_inline=*/false);

  // Drop the initial creation ref so `holder->tag` holds the sole reference to
  // `call` entering `Proceed`.
  grpc_call_unref(call);

  holder->tag.force_run(true);

  hook_done.WaitForNotification();
  EXPECT_TRUE(callback_ran);

  SetGlobalCallbackHook(new DefaultGlobalCallbackHook());
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
