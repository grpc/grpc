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

#include <cstdint>
#include <functional>
#include <new>
#include <utility>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/call.h"
#include "src/core/util/notification.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"

namespace grpc {
namespace internal {
namespace {

class TestCompletionQueueTag : public CompletionQueueTag {
 public:
  explicit TestCompletionQueueTag(std::function<void()> on_finalize = nullptr,
                                  bool result = true)
      : on_finalize_(std::move(on_finalize)), result_(result) {}

  bool FinalizeResult(void** /*tag*/, bool* /*status*/) override {
    if (on_finalize_) {
      on_finalize_();
    }
    return result_;
  }

 private:
  std::function<void()> on_finalize_;
  bool result_;
};

struct Holder {
  uint32_t magic = 0xCAFEBABEu;
  CallbackWithSuccessTag tag;
  ~Holder() { magic = 0u; }
};

// Managed on the call's Arena via Arena::ManagedNew so its destructor runs
// inside Arena::~Arena() when the final reference to `grpc_call` is dropped.
struct CallDestructionNotifier {
  explicit CallDestructionNotifier(grpc_core::Notification* destroyed)
      : destroyed(destroyed) {}
  ~CallDestructionNotifier() { destroyed->Notify(); }
  grpc_core::Notification* destroyed;
};

class ScopedGlobalCallbackHook {
 public:
  explicit ScopedGlobalCallbackHook(GlobalCallbackHook* hook) {
    SetGlobalCallbackHook(hook);
  }
  ~ScopedGlobalCallbackHook() {
    SetGlobalCallbackHook(new DefaultGlobalCallbackHook());
  }

  ScopedGlobalCallbackHook(const ScopedGlobalCallbackHook&) = delete;
  ScopedGlobalCallbackHook& operator=(const ScopedGlobalCallbackHook&) = delete;
};

class ArenaCheckingCallbackHook final : public GlobalCallbackHook {
 public:
  ArenaCheckingCallbackHook(grpc_core::Notification* call_destroyed,
                            grpc_core::Notification* hook_done)
      : call_destroyed_(call_destroyed), hook_done_(hook_done) {}

  void RunCallback(grpc_call* call,
                   absl::FunctionRef<void()> callback) override {
    ASSERT_NE(call, nullptr);
    uint32_t* pre_marker =
        static_cast<uint32_t*>(grpc_call_arena_alloc(call, sizeof(uint32_t)));
    ASSERT_NE(pre_marker, nullptr);
    *pre_marker = 0x12345678u;
    EXPECT_FALSE(call_destroyed_->HasBeenNotified());

    CatchingCallback(callback);

    // Verify that `call` and its arena remain alive after `callback()` has
    // destroyed the owning object and cleared the tag.
    EXPECT_FALSE(call_destroyed_->HasBeenNotified());
    EXPECT_EQ(*pre_marker, 0x12345678u);
    uint32_t* post_marker =
        static_cast<uint32_t*>(grpc_call_arena_alloc(call, sizeof(uint32_t)));
    ASSERT_NE(post_marker, nullptr);
    *post_marker = 0x87654321u;
    hook_done_->Notify();
  }

 private:
  grpc_core::Notification* call_destroyed_;
  grpc_core::Notification* hook_done_;
};

TEST(CallbackCommonTest,
     CallbackWithSuccessTagDestructionInsideCallbackKeepsCallAliveForHook) {
  ScopedGlobalCallbackHook default_hook_guard(new DefaultGlobalCallbackHook());
  grpc_init();
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_channel* channel = grpc_lame_client_channel_create(
      "fake_target", GRPC_STATUS_UNAVAILABLE, "test");
  grpc_slice host = grpc_slice_from_static_string("foo.test.google.com");
  grpc_call* call = grpc_channel_create_call(
      channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/service/method"), &host,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_core::Notification call_destroyed;
  grpc_core::Call::FromC(call)->arena()->ManagedNew<CallDestructionNotifier>(
      &call_destroyed);

  grpc_core::Notification hook_done;
  ScopedGlobalCallbackHook custom_hook_guard(
      new ArenaCheckingCallbackHook(&call_destroyed, &hook_done));

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
        EXPECT_EQ(holder->magic, 0xCAFEBABEu);
        callback_ran = true;
        holder->~Holder();
      },
      &test_ops, /*can_inline=*/false);

  // Drop the initial creation ref so `holder->tag` holds the sole reference to
  // `call` entering `Proceed`.
  grpc_call_unref(call);

  holder->tag.force_run(true);

  hook_done.WaitForNotification();
  call_destroyed.WaitForNotification();
  EXPECT_TRUE(callback_ran);

  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();
}

TEST(CallbackCommonTest,
     CallbackWithSuccessTagClearedDuringFinalizeResultSkipsCallback) {
  ScopedGlobalCallbackHook default_hook_guard(new DefaultGlobalCallbackHook());
  grpc_init();
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_channel* channel = grpc_lame_client_channel_create(
      "fake_target", GRPC_STATUS_UNAVAILABLE, "test");
  grpc_slice host = grpc_slice_from_static_string("foo.test.google.com");
  grpc_call* call = grpc_channel_create_call(
      channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/service/method"), &host,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_core::Notification call_destroyed;
  grpc_core::Call::FromC(call)->arena()->ManagedNew<CallDestructionNotifier>(
      &call_destroyed);

  grpc_core::Notification finalize_done;
  bool callback_ran = false;
  auto* holder = new (grpc_call_arena_alloc(call, sizeof(Holder))) Holder();
  TestCompletionQueueTag test_ops([holder, &call_destroyed, &finalize_done]() {
    // Destroy the owner (which calls ~CallbackWithSuccessTag() -> Clear())
    // during FinalizeResult. Proceed's pre-FinalizeResult ref must keep `call`
    // and its arena alive until Proceed finishes its own unref.
    holder->~Holder();
    EXPECT_FALSE(call_destroyed.HasBeenNotified());
    finalize_done.Notify();
  });

  holder->tag.Set(
      call,
      [holder, &callback_ran](bool /*ok*/) {
        callback_ran = true;
        EXPECT_EQ(holder->magic, 0xCAFEBABEu);
      },
      &test_ops, /*can_inline=*/false);

  // Drop the initial creation ref so `holder->tag` holds the sole reference to
  // `call` entering `Proceed`.
  grpc_call_unref(call);

  holder->tag.force_run(true);

  finalize_done.WaitForNotification();
  // Wait for Proceed's final grpc_call_unref(call) to destroy the call and its
  // arena before tearing down the channel, completion queue, and gRPC.
  call_destroyed.WaitForNotification();
  EXPECT_FALSE(callback_ran);

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
