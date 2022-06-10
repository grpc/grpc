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

#include <limits>
#include <queue>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/transport/chttp2/flow_control_fuzzer.pb.h"

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
  grpc_core::TestOnlySetProcessEpoch(g_now);
  gpr_now_impl = now_impl;
}

class FlowControlFuzzer {
 public:
  explicit FlowControlFuzzer(bool enable_bdp) {
    ExecCtx exec_ctx;
    tfc_ = absl::make_unique<TransportFlowControl>("fuzzer", enable_bdp,
                                                   &memory_owner_);
  }

  void Perform(const flow_control_fuzzer::Action& action);
  void AssertNoneStuck();

 private:
  struct StreamPayload {
    uint32_t id;
    uint64_t size;
  };

  struct SendToRemote {
    absl::optional<uint32_t> initial_window_size;
    uint32_t transport_window_update;
    std::vector<StreamPayload> stream_window_updates;
  };

  struct SendFromRemote {
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
};

void FlowControlFuzzer::Perform(const flow_control_fuzzer::Action& action) {
  ExecCtx exec_ctx;
  switch (action.action_case()) {
    case flow_control_fuzzer::Action::ACTION_NOT_SET:
      break;
    case flow_control_fuzzer::Action::kSetMemoryQuota: {
      memory_quota_->SetSize(
          Clamp(action.set_memory_quota(), uint64_t(1),
                uint64_t(std::numeric_limits<int64_t>::max())));
    } break;
    case flow_control_fuzzer::Action::kStepTimeMs: {
      g_now = gpr_time_add(
          g_now, gpr_time_from_millis(Clamp(action.step_time_ms(), uint64_t(1),
                                            kMaxAdvanceTimeMillis),
                                      GPR_TIMESPAN));
    } break;
    case flow_control_fuzzer::Action::kPeriodicUpdate: {
      PerformAction(tfc_->PeriodicUpdate(), nullptr);
    } break;
    case flow_control_fuzzer::Action::kPerformSendToRemote: {
      scheduled_write_ = true;
    } break;
    case flow_control_fuzzer::Action::kReadSendToRemote: {
      if (send_to_remote_.empty()) break;
      auto sent_to_remote = send_to_remote_.front();
      if (sent_to_remote.initial_window_size.has_value()) {
        SendFromRemote send_from_remote;
        send_from_remote.ack_initial_window_size =
            sent_to_remote.initial_window_size;
        remote_initial_window_size_ = *sent_to_remote.initial_window_size;
        send_from_remote_.push_back(send_from_remote);
      }
      for (auto stream_update : sent_to_remote.stream_window_updates) {
        GetStream(stream_update.id)->window_delta += stream_update.size;
      }
      remote_transport_window_size_ += sent_to_remote.transport_window_update;
      send_to_remote_.pop_front();
    } break;
    case flow_control_fuzzer::Action::kReadSendFromRemote: {
      if (send_from_remote_.empty()) break;
      auto sent_from_remote = send_from_remote_.front();
      if (sent_from_remote.ack_initial_window_size.has_value()) {
        tfc_->SetAckedInitialWindow(*sent_from_remote.ack_initial_window_size);
        PerformAction(tfc_->MakeAction(), nullptr);
        sending_initial_window_size_ = false;
      }
      for (const auto& stream_write : sent_from_remote.stream_writes) {
        Stream* stream = GetStream(stream_write.id);
        GPR_ASSERT(stream->fc.RecvData(stream_write.size).ok());
        PerformAction(stream->fc.MakeAction(), stream);
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
        send.stream_writes.push_back({id_stream.first, uint64_t(send_amount)});
        id_stream.second.queued_writes -= send_amount;
        id_stream.second.window_delta -= send_amount;
        remote_transport_window_size_ -= send_amount;
      }
      send_from_remote_.push_back(send);
    } break;
    case flow_control_fuzzer::Action::kSetMinProgressSize: {
      Stream* s = GetStream(action.set_min_progress_size().id());
      s->fc.UpdateProgress(action.set_min_progress_size().size());
      PerformAction(s->fc.MakeAction(), s);
    } break;
  }
  if (scheduled_write_) {
    SendToRemote send;
    if (!sending_initial_window_size_ &&
        queued_initial_window_size_.has_value()) {
      sending_initial_window_size_ = true;
      send.initial_window_size =
          absl::exchange(queued_initial_window_size_, absl::nullopt);
      tfc_->SetSentInitialWindow(*send.initial_window_size);
    }
    while (!streams_to_update_.empty()) {
      auto* stream = GetStream(streams_to_update_.front());
      streams_to_update_.pop();
      send.stream_window_updates.push_back(
          {stream->id, stream->fc.MaybeSendUpdate()});
    }
    send.transport_window_update = tfc_->MaybeSendUpdate(false);
    queued_send_max_frame_size_.reset();
    send_to_remote_.emplace_back(std::move(send));
    scheduled_write_ = false;
  }
}

void FlowControlFuzzer::PerformAction(FlowControlAction action,
                                      Stream* stream) {
  if (!squelch) {
    fprintf(stderr, "[%" PRId64 "]: ACTION: %s\n",
            stream == nullptr ? int64_t(-1) : int64_t(stream->id),
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
    queued_initial_window_size_ = action.initial_window_size();
  });
  with_urgency(action.send_max_frame_size_update(), [this, &action]() {
    queued_send_max_frame_size_ = action.max_frame_size();
  });
}

void FlowControlFuzzer::AssertNoneStuck() {
  GPR_ASSERT(!scheduled_write_);

  // Reconcile all the values to get the view of the remote that is knowable to
  // the flow control system.
  std::map<uint32_t, int64_t> reconciled_stream_deltas;
  int64_t reconciled_transport_window = remote_transport_window_size_;
  int64_t reconciled_initial_window = remote_initial_window_size_;
  for (const auto& id_stream : streams_) {
    reconciled_stream_deltas[id_stream.first] = id_stream.second.window_delta;
  }

  // Anything that's been sent from flow control -> remote needs to be added to
  // the remote.
  for (const auto& send_to_remote : send_to_remote_) {
    if (send_to_remote.initial_window_size.has_value()) {
      reconciled_initial_window = *send_to_remote.initial_window_size;
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
              ", init_window_size=%" PRId64 ", min_progress_size=%" PRId64 "\n",
              id_stream.first, stream_window, reconciled_transport_window,
              reconciled_stream_deltas[id_stream.first],
              reconciled_initial_window,
              int64_t(id_stream.second.fc.min_progress_size()));
      abort();
    }
  }
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
  }
}
