//
//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_GOAWAY_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_GOAWAY_H

#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "absl/status/status.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/promise/wait_set.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_GOAWAY_LOG VLOG(2)

class GoawayInterface {
 public:
  virtual ~GoawayInterface() = default;
  // Returns a promise that will be resolved when a ping frame is sent and the
  // corresponding ack is received.
  virtual Promise<absl::Status> SendPingAndWaitForAck() = 0;

  // Triggers a transport write cycle.
  virtual void TriggerWriteCycle() = 0;
};

enum class GoawayState {
  kIdle,
  kGracefulGoawayScheduled,
  kGracefulGoawaySent,
  kImmediateGoawayRequested,
  kDone,
};

// Information needed to construct a GOAWAY frame.
struct GoawayArgs {
  uint32_t error_code = 0;
  Slice debug_data;
  uint32_t last_good_stream_id = 0;
};

class GoawayManager {
 private:
  // Runs the given promise until the GOAWAY state is kDone.
  template <typename PromiseType>
  auto UntilDone(PromiseType&& promise) {
    return Race(
        [ctx = context_->Ref()]() -> Poll<absl::Status> {
          if (ctx->goaway_state == GoawayState::kDone) {
            GRPC_HTTP2_GOAWAY_LOG << "GOAWAY state is kDone. Resolving the "
                                     "promise with OK status.";
            return absl::OkStatus();
          }
          ctx->wakers.AddPending(GetContext<Activity>()->MakeNonOwningWaker());
          return Pending{};
        },
        std::forward<PromiseType>(promise));
  }

  // Handles an immediate GOAWAY request. The flow is as follows:
  // 1. If there is no pending GOAWAY(state is kIdle), then
  //    a. Set the GOAWAY state to kImmediateGoawayRequested.
  //    b. Set the GOAWAY args.
  //    c. Trigger a write cycle.
  //    d. Once the transport invokes MaybeGetSerializedGoawayFrames,
  //       a GOAWAY frame is sent and the state is changed to kDone effectively
  //       completing the GOAWAY process (and resolving the promise).
  // 2. If there is already an Immediate GOAWAY request in progress, then
  //    the function allows the previous error/debug data to take precedence
  //    and returns a pending promise. In this case, the promise resolves when
  //    the previous GOAWAY request completes.
  // 3. If there is a graceful GOAWAY request in progress(state is either
  //    kGracefulGoawayScheduled or kGracefulGoawaySent), then the immediate
  //    GOAWAY request takes precedence and the current error/debug data will be
  //    sent in the next transport write cycle. The graceful GOAWAY request
  //    will be effectively cancelled. The promise resolves when the immediate
  //    GOAWAY request completes.
  auto HandleImmediateGoaway(Http2ErrorCode error_code,
                             absl::string_view debug_data,
                             uint32_t last_good_stream_id) {
    return [ctx = context_->Ref(), error_code, debug_data,
            last_good_stream_id]() -> Poll<absl::Status> {
      if (ctx->goaway_state == GoawayState::kImmediateGoawayRequested) {
        GRPC_HTTP2_GOAWAY_LOG
            << "[Immediate GOAWAY] request already in progress.";
        // Previous GOAWAY request error/debug data takes precedence.
        return Pending{};
      }
      GRPC_HTTP2_GOAWAY_LOG << "[Immediate GOAWAY] state change "
                            << ctx->GoawayStateToString(ctx->goaway_state)
                            << " -> "
                               "kImmediateGoawayRequested.";

      ctx->goaway_state = GoawayState::kImmediateGoawayRequested;
      ctx->SetGoawayArgs(
          /*error_code=*/static_cast<uint32_t>(error_code),
          /*debug_data=*/Slice::FromCopiedString(debug_data),
          /*last_good_stream_id=*/last_good_stream_id);
      ctx->goaway_interface->TriggerWriteCycle();
      return Pending{};
    };
  }

  // Handles a graceful GOAWAY request. The flow is as follows:
  // 1. If there is no pending GOAWAY(state is kIdle), then
  //    a. Set the GOAWAY state to kGracefulGoawayScheduled.
  //    b. Set the GOAWAY args.
  //    c. Trigger a write cycle and request a ping request.
  //    d. Once the ping ack is received and the state is
  //       kGracefulGoawayScheduled, the state is changed to
  //       kGracefulGoawaySent and a write cycle is triggered. The state is
  //       checked again as an immediate GOAWAY request could have been made in
  //       between in which case the current graceful GOAWAY request is
  //       effectively cancelled. In either case, the promise resolves when the
  //       GOAWAY request completes.
  //    e. Once the state is kGracefulGoawaySent, the transport write cycle
  //       will send a GOAWAY frame and the state is changed to kDone
  //       effectively completing the GOAWAY process (and resolving the
  //       promise).
  // 2. If the state is anything other than kIdle, then we don't need to
  //    start a new graceful GOAWAY request. The promise resolves when the
  //    previous GOAWAY request completes.

  auto HandleGracefulGoaway(Http2ErrorCode error_code,
                            absl::string_view debug_data,
                            uint32_t last_good_stream_id) {
    return If(
        context_->goaway_state == GoawayState::kIdle,
        [ctx = context_->Ref(), error_code, debug_data, last_good_stream_id]() {
          // Only begin the graceful GOAWAY process (at most once)
          // if there is no pending GOAWAY.
          GRPC_HTTP2_GOAWAY_LOG << "[Graceful GOAWAY] state change "
                                << ctx->GoawayStateToString(ctx->goaway_state)
                                << " -> "
                                   "kGracefulGoawayScheduled.";
          ctx->goaway_state = GoawayState::kGracefulGoawayScheduled;
          ctx->SetGoawayArgs(
              /*error_code=*/static_cast<uint32_t>(error_code),
              /*debug_data=*/Slice::FromCopiedString(debug_data),
              /*last_good_stream_id=*/last_good_stream_id);
          ctx->goaway_interface->TriggerWriteCycle();
          return TrySeq(
              ctx->goaway_interface->SendPingAndWaitForAck(),
              [ctx]() -> Poll<absl::Status> {
                GRPC_HTTP2_GOAWAY_LOG
                    << "Ping resolved. Current state: "
                    << ctx->GoawayStateToString(ctx->goaway_state);
                if (ctx->goaway_state ==
                    GoawayState::kGracefulGoawayScheduled) {
                  GRPC_HTTP2_GOAWAY_LOG
                      << " [Graceful GOAWAY] state change "
                      << ctx->GoawayStateToString(ctx->goaway_state) << " -> "
                         "kGracefulGoawaySent.";
                  ctx->goaway_state = GoawayState::kGracefulGoawaySent;
                  ctx->goaway_interface->TriggerWriteCycle();
                }
                return Pending{};
              });
        },
        []() -> Poll<absl::Status> {
          GRPC_HTTP2_GOAWAY_LOG
              << "GOAWAY request already in progress.";
          return Pending{};
        });
  }

 public:
  GoawayManager(std::unique_ptr<GoawayInterface> goaway_interface);

  // Returns a promise that will be resolved when the GOAWAY process is
  // complete. For immediate GOAWAY, the promise will be resolved once the
  // GOAWAY frame is sent. For graceful GOAWAY, the promise will be resolved
  // once the final GOAWAY frame is sent.
  auto RequestGoaway(Http2ErrorCode error_code, absl::string_view debug_data,
                     uint32_t last_good_stream_id, bool immediate) {
    return AssertResultType<absl::Status>(
        UntilDone(If(
            immediate,
            [this, error_code, debug_data, last_good_stream_id]() {
              return AssertResultType<absl::Status>(HandleImmediateGoaway(
                  error_code, debug_data, last_good_stream_id));
            },
            [this, error_code, debug_data, last_good_stream_id]() {
              return AssertResultType<absl::Status>(HandleGracefulGoaway(
                  error_code, debug_data, last_good_stream_id));
            })));
  }

  // Called from the transport write cycle to serialize the GOAWAY frame if
  // needed.
  void MaybeGetSerializedGoawayFrame(SliceBuffer& output_buf);

  // Called from the transport write cycle to notify the GOAWAY manager that a
  // GOAWAY frame may have been sent. If a GOAWAY frame is sent in current
  // write cycle, this function handles the needed state transition.
  void NotifyGoawaySent();

  // Returns the current GOAWAY state.
  GoawayState TestOnlyGetGoawayState() const { return context_->goaway_state; }
  std::optional<Http2Frame> TestOnlyMaybeGetGoawayFrame();

 private:
  // Context for the GOAWAY process. This is passed around as a ref to all the
  // promises to ensure that the all the promises have valid access to the
  // context.
  struct Context : public RefCounted<Context> {
    explicit Context(std::unique_ptr<GoawayInterface> goaway_interface)
        : goaway_interface(std::move(goaway_interface)) {}

    Http2Frame GetGoawayFrame() {
      GRPC_HTTP2_GOAWAY_LOG
          << "GetGoawayFrame: "
          << " error code: " << goaway_args.error_code
          << " last good stream id: " << goaway_args.last_good_stream_id
          << " debug data: " << goaway_args.debug_data.as_string_view();

      return Http2GoawayFrame{
          /*last_stream_id=*/goaway_args.last_good_stream_id,
          /*error_code=*/goaway_args.error_code,
          /*debug_data=*/std::move(goaway_args.debug_data)};
    }

    Http2Frame GetGracefulGoawayFrame() {
      GRPC_HTTP2_GOAWAY_LOG
          << "GetGracefulGoawayFrame: "
          << " error code: " << goaway_args.error_code
          << " last good stream id: " << goaway_args.last_good_stream_id
          << " debug data: " << goaway_args.debug_data.as_string_view();
      return Http2GoawayFrame{
          /*last_stream_id=*/RFC9113::kMaxStreamId31Bit,
          /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kNoError),
          /*debug_data=*/
          Slice::FromCopiedBuffer(goaway_args.debug_data.as_string_view())};
    }

    void AddWaker(Waker&& waker) { wakers.AddPending(std::move(waker)); }

    void SetGoawayArgs(const uint32_t error_code, Slice&& debug_data,
                       const uint32_t last_good_stream_id) {
      GRPC_HTTP2_GOAWAY_LOG << "SetGoawayArgs: "
                            << " error code: " << error_code
                            << " last good stream id: " << last_good_stream_id
                            << " debug data: " << debug_data.as_string_view();
      goaway_args = GoawayArgs{.error_code = error_code,
                               .debug_data = std::move(debug_data),
                               .last_good_stream_id = last_good_stream_id};
    }

    std::string GoawayStateToString(GoawayState goaway_state);

    inline void SentGoawayTransition();

    GoawayState goaway_state = GoawayState::kIdle;
    std::unique_ptr<GoawayInterface> goaway_interface;
    // TODO(akshitpatel) : [PH2][P3] : There is scope to make this
    // IntractivityWaker.
    WaitSet wakers;
    GoawayArgs goaway_args{};
  };

  RefCountedPtr<Context> context_;
  bool goaway_sent_ = false;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_GOAWAY_H
