// Copyright 2022 gRPC authors.
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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/memory_request.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/transport/chttp2/flow_control_fuzzer.pb.h"

// IWYU pragma: no_include <google/protobuf/repeated_ptr_field.h>

bool squelch = true;

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_core {
namespace chttp2 {
namespace {

constexpr uint64_t kMaxAdvanceTimeMillis = 24ull * 365 * 3600 * 1000;

gpr_timespec g_now;
gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  gpr_timespec ts = g_now;
  ts.clock_type = clock_type;
  return ts;
}

void InitGlobals() {
  g_now = {1, 0, GPR_CLOCK_MONOTONIC};
  TestOnlySetProcessEpoch(g_now);
  gpr_now_impl = now_impl;
}

class FlowControlFuzzer {
 public:
  explicit FlowControlFuzzer(bool enable_bdp) {
    ExecCtx exec_ctx;
    tfc_ = std::make_unique<TransportFlowControl>("fuzzer", enable_bdp,
                                                  &memory_owner_);
  }

  ~FlowControlFuzzer() {
    ExecCtx exec_ctx;
    streams_.clear();
    tfc_.reset();
    memory_owner_.Release(allocated_memory_);
  }

  void Perform(const flow_control_fuzzer::Action& action);
  void AssertNoneStuck() const;
  void AssertAnnouncedOverInitialWindowSizeCorrect() const;

 private:
  struct StreamPayload {
    uint32_t id;
    uint64_t size;
  };

  struct SendToRemote {
    bool bdp_ping = false;
    absl::optional<uint32_t> initial_window_size;
    uint32_t transport_window_update;
    std::vector<StreamPayload> stream_window_updates;
  };

  struct SendFromRemote {
    bool bdp_pong = false;
    absl::optional<uint32_t> ack_initial_window_size;
    std::vector<StreamPayload> stream_writes;
  };

  struct Stream {
    explicit Stream(uint32_t id, TransportFlowControl* tfc) : id(id), fc(tfc) {}
    uint32_t id;
    StreamFlowControl fc;
    int64_t queued_writes = 0;
    int64_t window_delta = 0;
  };

  void PerformAction(FlowControlAction action, Stream* stream);
  Stream* GetStream(uint32_t id) {
    auto it = streams_.find(id);
    if (it == streams_.end()) {
      it = streams_.emplace(id, Stream{id, tfc_.get()}).first;
    }
    return &it->second;
  }

  MemoryQuotaRefPtr memory_quota_ = MakeMemoryQuota("fuzzer");
  MemoryOwner memory_owner_ = memory_quota_->CreateMemoryOwner("owner");
  std::unique_ptr<TransportFlowControl> tfc_;
  absl::optional<uint32_t> queued_initial_window_size_;
  absl::optional<uint32_t> queued_send_max_frame_size_;
  bool scheduled_write_ = false;
  bool sending_initial_window_size_ = false;
  std::deque<SendToRemote> send_to_remote_;
  std::deque<SendFromRemote> send_from_remote_;
  uint32_t remote_initial_window_size_ = kDefaultWindow;
  int64_t remote_transport_window_size_ = kDefaultWindow;
  std::map<uint32_t, Stream> streams_;
  std::queue<uint32_t> streams_to_update_;
  uint64_t allocated_memory_ = 0;
  Timestamp next_bdp_ping_ = Timestamp::ProcessEpoch();
};

void FlowControlFuzzer::Perform(const flow_control_fuzzer::Action& action) {
  ExecCtx exec_ctx;
  bool sending_payload = false;
  switch (action.action_case()) {
    case flow_control_fuzzer::Action::ACTION_NOT_SET:
      break;
    case flow_control_fuzzer::Action::kSetMemoryQuota: {
      memory_quota_->SetSize(
          Clamp(action.set_memory_quota(), uint64_t{1},
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
    } break;
    case flow_control_fuzzer::Action::kStepTimeMs: {
      g_now = gpr_time_add(
          g_now, gpr_time_from_millis(Clamp(action.step_time_ms(), uint64_t{1},
                                            kMaxAdvanceTimeMillis),
                                      GPR_TIMESPAN));
      exec_ctx.InvalidateNow();
      if (Timestamp::Now() >= next_bdp_ping_) {
        scheduled_write_ = true;
      }
    } break;
    case flow_control_fuzzer::Action::kPeriodicUpdate: {
      PerformAction(tfc_->PeriodicUpdate(), nullptr);
    } break;
    case flow_control_fuzzer::Action::kPerformSendToRemote: {
      scheduled_write_ = true;
    } break;
    case flow_control_fuzzer::Action::kPerformSendToRemoteWithPayload: {
      scheduled_write_ = true;
      sending_payload = true;
    } break;
    case flow_control_fuzzer::Action::kReadSendToRemote: {
      if (send_to_remote_.empty()) break;
      auto sent_to_remote = send_to_remote_.front();
      if (sent_to_remote.initial_window_size.has_value()) {
        if (!squelch) {
          fprintf(stderr, "Setting initial window size to %d\n",
                  sent_to_remote.initial_window_size.value());
        }
        SendFromRemote send_from_remote;
        send_from_remote.ack_initial_window_size =
            sent_to_remote.initial_window_size;
        for (const auto& id_stream : streams_) {
          GPR_ASSERT(id_stream.second.window_delta +
                         *sent_to_remote.initial_window_size <=
                     (1u << 31) - 1);
        }
        remote_initial_window_size_ = *sent_to_remote.initial_window_size;
        send_from_remote_.push_back(send_from_remote);
      }
      if (sent_to_remote.bdp_ping) {
        SendFromRemote send_from_remote;
        send_from_remote.bdp_pong = true;
        send_from_remote_.push_back(send_from_remote);
      }
      for (auto stream_update : sent_to_remote.stream_window_updates) {
        Stream* s = GetStream(stream_update.id);
        if (!squelch) {
          fprintf(stderr,
                  "[%" PRIu32 "]: increase window delta by %" PRIu64
                  " from %" PRId64 "\n",
                  stream_update.id, stream_update.size, s->window_delta);
        }
        s->window_delta += stream_update.size;
        GPR_ASSERT(s->window_delta <= chttp2::kMaxWindowDelta);
      }
      remote_transport_window_size_ += sent_to_remote.transport_window_update;
      send_to_remote_.pop_front();
    } break;
    case flow_control_fuzzer::Action::kReadSendFromRemote: {
      if (send_from_remote_.empty()) break;
      auto sent_from_remote = send_from_remote_.front();
      if (sent_from_remote.ack_initial_window_size.has_value()) {
        if (!squelch) {
          fprintf(stderr, "Received ACK for initial window size %d\n",
                  *sent_from_remote.ack_initial_window_size);
        }
        PerformAction(tfc_->SetAckedInitialWindow(
                          *sent_from_remote.ack_initial_window_size),
                      nullptr);
        sending_initial_window_size_ = false;
      }
      if (sent_from_remote.bdp_pong) {
        next_bdp_ping_ = tfc_->bdp_estimator()->CompletePing();
      }
      for (const auto& stream_write : sent_from_remote.stream_writes) {
        Stream* stream = GetStream(stream_write.id);
        if (!squelch) {
          fprintf(stderr, "[%" PRIu32 "]: recv write of %" PRIu64 "\n",
                  stream_write.id, stream_write.size);
        }
        if (auto* bdp = tfc_->bdp_estimator()) {
          bdp->AddIncomingBytes(stream_write.size);
        }
        StreamFlowControl::IncomingUpdateContext upd(&stream->fc);
        GPR_ASSERT(upd.RecvData(stream_write.size).ok());
        PerformAction(upd.MakeAction(), stream);
      }
      send_from_remote_.pop_front();
    } break;
    case flow_control_fuzzer::Action::kStreamWrite: {
      Stream* s = GetStream(action.stream_write().id());
      s->queued_writes += action.stream_write().size();
    } break;
    case flow_control_fuzzer::Action::kPerformSendFromRemote: {
      SendFromRemote send;
      for (auto& id_stream : streams_) {
        auto send_amount = std::min(
            {id_stream.second.queued_writes, remote_transport_window_size_,
             remote_initial_window_size_ + id_stream.second.window_delta});
        if (send_amount <= 0) continue;
        send.stream_writes.push_back(
            {id_stream.first, static_cast<uint64_t>(send_amount)});
        id_stream.second.queued_writes -= send_amount;
        id_stream.second.window_delta -= send_amount;
        remote_transport_window_size_ -= send_amount;
      }
      send_from_remote_.push_back(send);
    } break;
    case flow_control_fuzzer::Action::kSetMinProgressSize: {
      Stream* s = GetStream(action.set_min_progress_size().id());
      StreamFlowControl::IncomingUpdateContext upd(&s->fc);
      upd.SetMinProgressSize(action.set_min_progress_size().size());
      PerformAction(upd.MakeAction(), s);
    } break;
    case flow_control_fuzzer::Action::kAllocateMemory: {
      auto allocate = std::min(
          static_cast<size_t>(action.allocate_memory()),
          grpc_event_engine::experimental::MemoryRequest::max_allowed_size());
      allocated_memory_ += allocate;
      memory_owner_.Reserve(allocate);
    } break;
    case flow_control_fuzzer::Action::kDeallocateMemory: {
      auto deallocate = std::min(
          static_cast<uint64_t>(action.deallocate_memory()), allocated_memory_);
      allocated_memory_ -= deallocate;
      memory_owner_.Release(deallocate);
    } break;
    case flow_control_fuzzer::Action::kSetPendingSize: {
      Stream* s = GetStream(action.set_min_progress_size().id());
      StreamFlowControl::IncomingUpdateContext upd(&s->fc);
      upd.SetPendingSize(action.set_pending_size().size());
      PerformAction(upd.MakeAction(), s);
    } break;
  }
  if (scheduled_write_) {
    SendToRemote send;
    if (Timestamp::Now() >= next_bdp_ping_) {
      if (auto* bdp = tfc_->bdp_estimator()) {
        bdp->SchedulePing();
        bdp->StartPing();
        next_bdp_ping_ = Timestamp::InfFuture();
        send.bdp_ping = true;
      }
    }
    if (!sending_initial_window_size_ &&
        queued_initial_window_size_.has_value()) {
      sending_initial_window_size_ = true;
      send.initial_window_size =
          std::exchange(queued_initial_window_size_, absl::nullopt);
    }
    while (!streams_to_update_.empty()) {
      auto* stream = GetStream(streams_to_update_.front());
      streams_to_update_.pop();
      send.stream_window_updates.push_back(
          {stream->id, stream->fc.MaybeSendUpdate()});
    }
    send.transport_window_update = tfc_->MaybeSendUpdate(sending_payload);
    queued_send_max_frame_size_.reset();
    send_to_remote_.emplace_back(std::move(send));
    scheduled_write_ = false;
  }
}

void FlowControlFuzzer::PerformAction(FlowControlAction action,
                                      Stream* stream) {
  if (!squelch) {
    fprintf(stderr, "[%" PRId64 "]: ACTION: %s\n",
            stream == nullptr ? int64_t{-1} : static_cast<int64_t>(stream->id),
            action.DebugString().c_str());
  }

  auto with_urgency = [this](FlowControlAction::Urgency urgency,
                             std::function<void()> f) {
    switch (urgency) {
      case FlowControlAction::Urgency::NO_ACTION_NEEDED:
        break;
      case FlowControlAction::Urgency::UPDATE_IMMEDIATELY:
        scheduled_write_ = true;
        ABSL_FALLTHROUGH_INTENDED;
      case FlowControlAction::Urgency::QUEUE_UPDATE:
        f();
        break;
    }
  };
  with_urgency(action.send_stream_update(),
               [this, stream]() { streams_to_update_.push(stream->id); });
  with_urgency(action.send_transport_update(), []() {});
  with_urgency(action.send_initial_window_update(), [this, &action]() {
    GPR_ASSERT(action.initial_window_size() <= chttp2::kMaxInitialWindowSize);
    queued_initial_window_size_ = action.initial_window_size();
  });
  with_urgency(action.send_max_frame_size_update(), [this, &action]() {
    queued_send_max_frame_size_ = action.max_frame_size();
  });
}

void FlowControlFuzzer::AssertNoneStuck() const {
  GPR_ASSERT(!scheduled_write_);

  // Reconcile all the values to get the view of the remote that is knowable to
  // the flow control system.
  std::map<uint32_t, int64_t> reconciled_stream_deltas;
  int64_t reconciled_transport_window = remote_transport_window_size_;
  int64_t reconciled_initial_window = remote_initial_window_size_;
  std::vector<uint64_t> inflight_send_initial_windows;
  for (const auto& id_stream : streams_) {
    reconciled_stream_deltas[id_stream.first] = id_stream.second.window_delta;
  }

  // Anything that's been sent from flow control -> remote needs to be added to
  // the remote.
  for (const auto& send_to_remote : send_to_remote_) {
    if (send_to_remote.initial_window_size.has_value()) {
      reconciled_initial_window = *send_to_remote.initial_window_size;
      inflight_send_initial_windows.push_back(
          *send_to_remote.initial_window_size);
    }
    reconciled_transport_window += send_to_remote.transport_window_update;
    for (const auto& stream_update : send_to_remote.stream_window_updates) {
      reconciled_stream_deltas[stream_update.id] += stream_update.size;
    }
  }

  // Anything that's been sent from remote -> flow control needs to be wound
  // back into the remote.
  for (const auto& send_from_remote : send_from_remote_) {
    for (const auto& stream_write : send_from_remote.stream_writes) {
      reconciled_stream_deltas[stream_write.id] += stream_write.size;
      reconciled_transport_window += stream_write.size;
    }
  }

  // If we're sending an initial window size we get to consider a queued initial
  // window size too: it'll be sent as soon as the remote acks the settings
  // change, which it must.
  if (sending_initial_window_size_ && queued_initial_window_size_.has_value()) {
    reconciled_initial_window = *queued_initial_window_size_;
    inflight_send_initial_windows.push_back(*queued_initial_window_size_);
  }

  // Finally, if a stream has indicated it's willing to read, the reconciled
  // remote *MUST* be in a state where it could send at least one byte.
  for (const auto& id_stream : streams_) {
    if (id_stream.second.fc.min_progress_size() == 0) continue;
    int64_t stream_window =
        reconciled_stream_deltas[id_stream.first] + reconciled_initial_window;
    if (stream_window <= 0 || reconciled_transport_window <= 0) {
      fprintf(stderr,
              "FAILED: stream %d has stream_window=%" PRId64
              ", transport_window=%" PRId64 ", delta=%" PRId64
              ", init_window_size=%" PRId64 ", min_progress_size=%" PRId64
              ", transport announced_stream_total_over_incoming_window=%" PRId64
              ", transport announced_window=%" PRId64
              " transport target_window=%" PRId64 "\n",
              id_stream.first, stream_window, reconciled_transport_window,
              reconciled_stream_deltas[id_stream.first],
              reconciled_initial_window,
              (id_stream.second.fc.min_progress_size()),
              tfc_->announced_stream_total_over_incoming_window(),
              tfc_->announced_window(), tfc_->target_window());
      fprintf(stderr,
              "initial_window breakdown: remote=%" PRId32 ", in-flight={%s}\n",
              remote_initial_window_size_,
              absl::StrJoin(inflight_send_initial_windows, ",").c_str());
      abort();
    }
  }
}

void FlowControlFuzzer::AssertAnnouncedOverInitialWindowSizeCorrect() const {
  int64_t value_from_streams = 0;

  for (const auto& id_stream : streams_) {
    const auto& stream = id_stream.second;
    if (stream.fc.announced_window_delta() > 0) {
      value_from_streams += stream.fc.announced_window_delta();
    }
  }

  GPR_ASSERT(value_from_streams ==
             tfc_->announced_stream_total_over_incoming_window());
}

}  // namespace
}  // namespace chttp2
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const flow_control_fuzzer::Msg& msg) {
  grpc_core::chttp2::InitGlobals();
  grpc_core::chttp2::FlowControlFuzzer fuzzer(msg.enable_bdp());
  for (const auto& action : msg.actions()) {
    if (!squelch) {
      fprintf(stderr, "%s\n", action.DebugString().c_str());
    }
    fuzzer.Perform(action);
    fuzzer.AssertNoneStuck();
    fuzzer.AssertAnnouncedOverInitialWindowSizeCorrect();
  }
}
