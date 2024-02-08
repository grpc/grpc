//
//
// Copyright 2023 gRPC authors.
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

#include "test/core/end2end/fuzzers/fuzzing_common.h"

#include <string.h>

#include <memory>
#include <new>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/byte_buffer.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"

namespace grpc_core {

namespace {
int force_experiments = []() {
  ForceEnableExperiment("event_engine_client", true);
  ForceEnableExperiment("event_engine_listener", true);
  return 1;
}();
}  // namespace

namespace testing {

static void free_non_null(void* p) {
  GPR_ASSERT(p != nullptr);
  gpr_free(p);
}

enum class CallType { CLIENT, SERVER, PENDING_SERVER, TOMBSTONED };

class Call : public std::enable_shared_from_this<Call> {
 public:
  explicit Call(CallType type) : type_(type) {
    grpc_metadata_array_init(&recv_initial_metadata_);
    grpc_metadata_array_init(&recv_trailing_metadata_);
    grpc_call_details_init(&call_details_);
  }

  ~Call();

  CallType type() const { return type_; }

  bool done() const {
    if ((type_ == CallType::TOMBSTONED || call_closed_) && pending_ops_ == 0) {
      return true;
    }
    if (call_ == nullptr && type() != CallType::PENDING_SERVER) return true;
    return false;
  }

  void Shutdown() {
    if (call_ != nullptr) {
      grpc_call_cancel(call_, nullptr);
      type_ = CallType::TOMBSTONED;
    }
  }

  void SetCall(grpc_call* call) {
    GPR_ASSERT(call_ == nullptr);
    call_ = call;
  }

  grpc_call* call() const { return call_; }

  void RequestCall(grpc_server* server, grpc_completion_queue* cq) {
    auto* v = FinishedRequestCall();
    grpc_call_error error = grpc_server_request_call(
        server, &call_, &call_details_, &recv_initial_metadata_, cq, cq, v);
    if (error != GRPC_CALL_OK) {
      v->Run(false);
    }
  }

  void* Allocate(size_t size) {
    void* p = gpr_malloc(size);
    free_pointers_.push_back(p);
    return p;
  }

  template <typename T>
  T* AllocArray(size_t elems) {
    return static_cast<T*>(Allocate(sizeof(T) * elems));
  }

  template <typename T>
  T* NewCopy(T value) {
    T* p = AllocArray<T>(1);
    new (p) T(value);
    return p;
  }

  template <typename T>
  grpc_slice ReadSlice(const T& s) {
    grpc_slice slice = grpc_slice_from_cpp_string(s.value());
    unref_slices_.push_back(slice);
    return slice;
  }

  template <typename M>
  grpc_metadata_array ReadMetadata(const M& metadata) {
    grpc_metadata* m = AllocArray<grpc_metadata>(metadata.size());
    for (int i = 0; i < metadata.size(); ++i) {
      m[i].key = ReadSlice(metadata[i].key());
      m[i].value = ReadSlice(metadata[i].value());
    }
    return grpc_metadata_array{static_cast<size_t>(metadata.size()),
                               static_cast<size_t>(metadata.size()), m};
  }

  absl::optional<grpc_op> ReadOp(
      const api_fuzzer::BatchOp& batch_op, bool* batch_is_ok,
      uint8_t* batch_ops, std::vector<std::function<void()>>* unwinders) {
    grpc_op op;
    memset(&op, 0, sizeof(op));
    switch (batch_op.op_case()) {
      case api_fuzzer::BatchOp::OP_NOT_SET:
        // invalid value
        return {};
      case api_fuzzer::BatchOp::kSendInitialMetadata:
        if (sent_initial_metadata_) {
          *batch_is_ok = false;
        } else {
          sent_initial_metadata_ = true;
          op.op = GRPC_OP_SEND_INITIAL_METADATA;
          *batch_ops |= 1 << GRPC_OP_SEND_INITIAL_METADATA;
          auto ary = ReadMetadata(batch_op.send_initial_metadata().metadata());
          op.data.send_initial_metadata.count = ary.count;
          op.data.send_initial_metadata.metadata = ary.metadata;
        }
        break;
      case api_fuzzer::BatchOp::kSendMessage:
        op.op = GRPC_OP_SEND_MESSAGE;
        if (send_message_ != nullptr) {
          *batch_is_ok = false;
        } else {
          *batch_ops |= 1 << GRPC_OP_SEND_MESSAGE;
          std::vector<grpc_slice> slices;
          for (const auto& m : batch_op.send_message().message()) {
            slices.push_back(ReadSlice(m));
          }
          send_message_ = op.data.send_message.send_message =
              grpc_raw_byte_buffer_create(slices.data(), slices.size());
          unwinders->push_back([this]() {
            grpc_byte_buffer_destroy(send_message_);
            send_message_ = nullptr;
          });
        }
        break;
      case api_fuzzer::BatchOp::kSendCloseFromClient:
        op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
        *batch_ops |= 1 << GRPC_OP_SEND_CLOSE_FROM_CLIENT;
        break;
      case api_fuzzer::BatchOp::kSendStatusFromServer: {
        op.op = GRPC_OP_SEND_STATUS_FROM_SERVER;
        *batch_ops |= 1 << GRPC_OP_SEND_STATUS_FROM_SERVER;
        auto ary = ReadMetadata(batch_op.send_status_from_server().metadata());
        op.data.send_status_from_server.trailing_metadata_count = ary.count;
        op.data.send_status_from_server.trailing_metadata = ary.metadata;
        op.data.send_status_from_server.status = static_cast<grpc_status_code>(
            batch_op.send_status_from_server().status_code());
        op.data.send_status_from_server.status_details =
            batch_op.send_status_from_server().has_status_details()
                ? NewCopy(ReadSlice(
                      batch_op.send_status_from_server().status_details()))
                : nullptr;
      } break;
      case api_fuzzer::BatchOp::kReceiveInitialMetadata:
        if (enqueued_recv_initial_metadata_) {
          *batch_is_ok = false;
        } else {
          enqueued_recv_initial_metadata_ = true;
          op.op = GRPC_OP_RECV_INITIAL_METADATA;
          *batch_ops |= 1 << GRPC_OP_RECV_INITIAL_METADATA;
          op.data.recv_initial_metadata.recv_initial_metadata =
              &recv_initial_metadata_;
        }
        break;
      case api_fuzzer::BatchOp::kReceiveMessage:
        // Allow only one active pending_recv_message_op to exist. Otherwise if
        // the previous enqueued recv_message_op is not complete by the time
        // we get here, then under certain conditions, enqueuing this op will
        // overwrite the internal call->receiving_buffer maintained by grpc
        // leading to a memory leak.
        if (call_closed_ || pending_recv_message_op_) {
          *batch_is_ok = false;
        } else {
          op.op = GRPC_OP_RECV_MESSAGE;
          *batch_ops |= 1 << GRPC_OP_RECV_MESSAGE;
          pending_recv_message_op_ = true;
          op.data.recv_message.recv_message = &recv_message_;
          unwinders->push_back([this]() { pending_recv_message_op_ = false; });
        }
        break;
      case api_fuzzer::BatchOp::kReceiveStatusOnClient:
        op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
        op.data.recv_status_on_client.status = &status_;
        op.data.recv_status_on_client.trailing_metadata =
            &recv_trailing_metadata_;
        op.data.recv_status_on_client.status_details = &recv_status_details_;
        *batch_ops |= 1 << GRPC_OP_RECV_STATUS_ON_CLIENT;
        break;
      case api_fuzzer::BatchOp::kReceiveCloseOnServer:
        op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
        *batch_ops |= 1 << GRPC_OP_RECV_CLOSE_ON_SERVER;
        op.data.recv_close_on_server.cancelled = &cancelled_;
        break;
    }
    op.reserved = nullptr;
    op.flags = batch_op.flags();
    return op;
  }

  Validator* FinishedBatchValidator(uint8_t has_ops) {
    ++pending_ops_;
    auto self = shared_from_this();
    return MakeValidator([self, has_ops](bool /*success*/) {
      --self->pending_ops_;
      if (has_ops & (1u << GRPC_OP_RECV_MESSAGE)) {
        self->pending_recv_message_op_ = false;
        if (self->recv_message_ != nullptr) {
          grpc_byte_buffer_destroy(self->recv_message_);
          self->recv_message_ = nullptr;
        }
      }
      if ((has_ops & (1u << GRPC_OP_SEND_MESSAGE))) {
        grpc_byte_buffer_destroy(self->send_message_);
        self->send_message_ = nullptr;
      }
      if ((has_ops & (1u << GRPC_OP_RECV_STATUS_ON_CLIENT)) ||
          (has_ops & (1u << GRPC_OP_RECV_CLOSE_ON_SERVER))) {
        self->call_closed_ = true;
      }
    });
  }

  Validator* FinishedRequestCall() {
    ++pending_ops_;
    auto self = shared_from_this();
    return MakeValidator([self](bool success) {
      GPR_ASSERT(self->pending_ops_ > 0);
      --self->pending_ops_;
      if (success) {
        GPR_ASSERT(self->call_ != nullptr);
        self->type_ = CallType::SERVER;
      } else {
        self->type_ = CallType::TOMBSTONED;
      }
    });
  }

 private:
  CallType type_;
  grpc_call* call_ = nullptr;
  grpc_byte_buffer* recv_message_ = nullptr;
  grpc_status_code status_;
  grpc_metadata_array recv_initial_metadata_{0, 0, nullptr};
  grpc_metadata_array recv_trailing_metadata_{0, 0, nullptr};
  grpc_slice recv_status_details_ = grpc_empty_slice();
  // set by receive close on server, unset here to trigger
  // msan if misused
  int cancelled_;
  int pending_ops_ = 0;
  bool sent_initial_metadata_ = false;
  bool enqueued_recv_initial_metadata_ = false;
  grpc_call_details call_details_{};
  grpc_byte_buffer* send_message_ = nullptr;
  bool call_closed_ = false;
  bool pending_recv_message_op_ = false;

  std::vector<void*> free_pointers_;
  std::vector<grpc_slice> unref_slices_;
};

Call::~Call() {
  if (call_ != nullptr) {
    grpc_call_unref(call_);
  }
  grpc_slice_unref(recv_status_details_);
  grpc_call_details_destroy(&call_details_);

  for (auto* p : free_pointers_) {
    gpr_free(p);
  }
  for (auto s : unref_slices_) {
    grpc_slice_unref(s);
  }

  if (recv_message_ != nullptr) {
    grpc_byte_buffer_destroy(recv_message_);
    recv_message_ = nullptr;
  }

  grpc_metadata_array_destroy(&recv_initial_metadata_);
  grpc_metadata_array_destroy(&recv_trailing_metadata_);
}

namespace {
Validator* ValidateConnectivityWatch(gpr_timespec deadline, int* counter) {
  return MakeValidator([deadline, counter](bool success) {
    if (!success) {
      auto now = gpr_now(deadline.clock_type);
      GPR_ASSERT(gpr_time_cmp(now, deadline) >= 0);
    }
    --*counter;
  });
}
}  // namespace

using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::SetEventEngineFactory;

BasicFuzzer::BasicFuzzer(const fuzzing_event_engine::Actions& actions)
    : engine_([actions]() {
        SetEventEngineFactory(
            [actions]() -> std::unique_ptr<
                            grpc_event_engine::experimental::EventEngine> {
              return std::make_unique<FuzzingEventEngine>(
                  FuzzingEventEngine::Options(), actions);
            });
        return std::dynamic_pointer_cast<FuzzingEventEngine>(
            GetDefaultEventEngine());
      }()) {
  grpc_timer_manager_set_start_threaded(false);
  grpc_init();
  {
    ExecCtx exec_ctx;
    Executor::SetThreadingAll(false);
  }
  resource_quota_ = MakeResourceQuota("fuzzer");
  cq_ = grpc_completion_queue_create_for_next(nullptr);
}

BasicFuzzer::~BasicFuzzer() {
  GPR_ASSERT(ActiveCall() == nullptr);
  GPR_ASSERT(calls_.empty());

  engine_->TickUntilIdle();

  grpc_completion_queue_shutdown(cq_);
  GPR_ASSERT(PollCq() == Result::kComplete);
  grpc_completion_queue_destroy(cq_);

  grpc_shutdown_blocking();
  engine_->UnsetGlobalHooks();
}

void BasicFuzzer::Tick() {
  engine_->Tick();
  grpc_timer_manager_tick();
}

BasicFuzzer::Result BasicFuzzer::PollCq() {
  grpc_event ev = grpc_completion_queue_next(
      cq_, gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
  switch (ev.type) {
    case GRPC_OP_COMPLETE: {
      static_cast<Validator*>(ev.tag)->Run(ev.success);
      break;
    }
    case GRPC_QUEUE_TIMEOUT:
      break;
    case GRPC_QUEUE_SHUTDOWN:
      return Result::kComplete;
  }
  return Result::kPending;
}

BasicFuzzer::Result BasicFuzzer::CheckConnectivity(bool try_to_connect) {
  if (channel() != nullptr) {
    grpc_channel_check_connectivity_state(channel(), try_to_connect);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::WatchConnectivity(uint32_t duration_us) {
  if (channel() != nullptr) {
    grpc_connectivity_state st =
        grpc_channel_check_connectivity_state(channel(), 0);
    if (st != GRPC_CHANNEL_SHUTDOWN) {
      gpr_timespec deadline =
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_micros(duration_us, GPR_TIMESPAN));
      grpc_channel_watch_connectivity_state(
          channel(), st, deadline, cq_,
          ValidateConnectivityWatch(deadline, &pending_channel_watches_));
      pending_channel_watches_++;
    }
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::CancelAllCallsIfShutdown() {
  if (server() != nullptr && server_shutdown_) {
    grpc_server_cancel_all_calls(server());
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::ShutdownServer() {
  if (server() != nullptr) {
    grpc_server_shutdown_and_notify(
        server(), cq_, AssertSuccessAndDecrement(&pending_server_shutdowns_));
    pending_server_shutdowns_++;
    server_shutdown_ = true;
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::DestroyServerIfReady() {
  if (server() != nullptr && server_shutdown_ &&
      pending_server_shutdowns_ == 0) {
    DestroyServer();
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::CreateCall(
    const api_fuzzer::CreateCall& create_call) {
  bool ok = true;
  if (channel() == nullptr) ok = false;
  // If the active call is a server call, then use it as the parent call
  // to exercise the propagation logic.
  Call* parent_call = ActiveCall();
  if (parent_call != nullptr && parent_call->type() != CallType::SERVER) {
    parent_call = nullptr;
  }
  calls_.emplace_back(new Call(CallType::CLIENT));
  grpc_slice method = calls_.back()->ReadSlice(create_call.method());
  if (GRPC_SLICE_LENGTH(method) == 0) {
    ok = false;
  }
  grpc_slice host = calls_.back()->ReadSlice(create_call.host());
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_micros(create_call.timeout(), GPR_TIMESPAN));

  if (ok) {
    calls_.back()->SetCall(grpc_channel_create_call(
        channel(), parent_call == nullptr ? nullptr : parent_call->call(),
        create_call.propagation_mask(), cq_, method, &host, deadline, nullptr));
  } else {
    calls_.pop_back();
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::ChangeActiveCall() {
  active_call_++;
  ActiveCall();
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::QueueBatchForActiveCall(
    const api_fuzzer::Batch& queue_batch) {
  auto* active_call = ActiveCall();
  if (active_call == nullptr ||
      active_call->type() == CallType::PENDING_SERVER ||
      active_call->call() == nullptr) {
    return Result::kFailed;
  }
  const auto& batch = queue_batch.operations();
  if (batch.size() > 6) {
    return Result::kFailed;
  }
  std::vector<grpc_op> ops;
  bool ok = true;
  uint8_t has_ops = 0;
  std::vector<std::function<void()>> unwinders;
  for (const auto& batch_op : batch) {
    auto op = active_call->ReadOp(batch_op, &ok, &has_ops, &unwinders);
    if (!op.has_value()) continue;
    ops.push_back(*op);
  }

  if (channel() == nullptr) ok = false;
  if (ok) {
    auto* v = active_call->FinishedBatchValidator(has_ops);
    grpc_call_error error = grpc_call_start_batch(
        active_call->call(), ops.data(), ops.size(), v, nullptr);
    if (error != GRPC_CALL_OK) {
      v->Run(false);
    }
  } else {
    for (auto& unwind : unwinders) {
      unwind();
    }
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::CancelActiveCall() {
  auto* active_call = ActiveCall();
  if (active_call != nullptr && active_call->call() != nullptr) {
    grpc_call_cancel(active_call->call(), nullptr);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::SendPingOnChannel() {
  if (channel() != nullptr) {
    pending_pings_++;
    grpc_channel_ping(channel(), cq_, Decrement(&pending_pings_), nullptr);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::Pause(Duration duration) {
  ++paused_;
  engine()->RunAfterExactly(duration, [this]() { --paused_; });
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::ServerRequestCall() {
  if (server() == nullptr) {
    return Result::kFailed;
  }
  calls_.emplace_back(new Call(CallType::PENDING_SERVER));
  calls_.back()->RequestCall(server(), cq_);
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::DestroyActiveCall() {
  auto* active_call = ActiveCall();
  if (active_call != nullptr &&
      active_call->type() != CallType::PENDING_SERVER &&
      active_call->call() != nullptr) {
    calls_[active_call_]->Shutdown();
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::ValidatePeerForActiveCall() {
  auto* active_call = ActiveCall();
  if (active_call != nullptr && active_call->call() != nullptr) {
    free_non_null(grpc_call_get_peer(active_call->call()));
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::ValidateChannelTarget() {
  if (channel() != nullptr) {
    free_non_null(grpc_channel_get_target(channel()));
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::ResizeResourceQuota(
    uint32_t resize_resource_quota) {
  ExecCtx exec_ctx;
  resource_quota_->memory_quota()->SetSize(resize_resource_quota);
  return Result::kComplete;
}

BasicFuzzer::Result BasicFuzzer::CloseChannel() {
  if (channel() != nullptr) {
    DestroyChannel();
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

Call* BasicFuzzer::ActiveCall() {
  while (!calls_.empty()) {
    if (active_call_ >= calls_.size()) {
      active_call_ = 0;
    }
    if (calls_[active_call_] != nullptr && !calls_[active_call_]->done()) {
      return calls_[active_call_].get();
    }
    calls_.erase(calls_.begin() + active_call_);
  }
  return nullptr;
}

void BasicFuzzer::ShutdownCalls() {
  for (auto& call : calls_) {
    if (call == nullptr) continue;
    if (call->type() == CallType::PENDING_SERVER) continue;
    call->Shutdown();
  }
}

bool BasicFuzzer::Continue() {
  return channel() != nullptr || server() != nullptr ||
         pending_channel_watches_ > 0 || pending_pings_ > 0 ||
         ActiveCall() != nullptr || paused_;
}

BasicFuzzer::Result BasicFuzzer::ExecuteAction(
    const api_fuzzer::Action& action) {
  gpr_log(GPR_DEBUG, "EXECUTE_ACTION: %s", action.DebugString().c_str());
  switch (action.type_case()) {
    case api_fuzzer::Action::TYPE_NOT_SET:
      return BasicFuzzer::Result::kFailed;
    // tickle completion queue
    case api_fuzzer::Action::kPollCq:
      return PollCq();
    // create an insecure channel
    case api_fuzzer::Action::kCreateChannel:
      return CreateChannel(action.create_channel());
    // destroy a channel
    case api_fuzzer::Action::kCloseChannel:
      return CloseChannel();
    // bring up a server
    case api_fuzzer::Action::kCreateServer:
      return CreateServer(action.create_server());
    // begin server shutdown
    case api_fuzzer::Action::kShutdownServer:
      return ShutdownServer();
    // cancel all calls if server is shutdown
    case api_fuzzer::Action::kCancelAllCallsIfShutdown:
      return CancelAllCallsIfShutdown();
    // destroy server
    case api_fuzzer::Action::kDestroyServerIfReady:
      return DestroyServerIfReady();
    // check connectivity
    case api_fuzzer::Action::kCheckConnectivity:
      return CheckConnectivity(action.check_connectivity());
    // watch connectivity
    case api_fuzzer::Action::kWatchConnectivity:
      return WatchConnectivity(action.watch_connectivity());
    // create a call
    case api_fuzzer::Action::kCreateCall:
      return CreateCall(action.create_call());
    // switch the 'current' call
    case api_fuzzer::Action::kChangeActiveCall:
      return ChangeActiveCall();
    // queue some ops on a call
    case api_fuzzer::Action::kQueueBatch:
      return QueueBatchForActiveCall(action.queue_batch());
    // cancel current call
    case api_fuzzer::Action::kCancelCall:
      return CancelActiveCall();
    // get a calls peer
    case api_fuzzer::Action::kGetPeer:
      return ValidatePeerForActiveCall();
    // get a channels target
    case api_fuzzer::Action::kGetTarget:
      return ValidateChannelTarget();
    // send a ping on a channel
    case api_fuzzer::Action::kPing:
      return SendPingOnChannel();
    // enable a tracer
    case api_fuzzer::Action::kEnableTracer: {
      grpc_tracer_set_enabled(action.enable_tracer().c_str(), 1);
      break;
    }
    // disable a tracer
    case api_fuzzer::Action::kDisableTracer: {
      grpc_tracer_set_enabled(action.disable_tracer().c_str(), 0);
      break;
    }
    // request a server call
    case api_fuzzer::Action::kRequestCall:
      return ServerRequestCall();
    // destroy a call
    case api_fuzzer::Action::kDestroyCall:
      return DestroyActiveCall();
    // resize the buffer pool
    case api_fuzzer::Action::kResizeResourceQuota:
      return ResizeResourceQuota(action.resize_resource_quota());
    case api_fuzzer::Action::kSleepMs:
      return Pause(std::min(Duration::Milliseconds(action.sleep_ms()),
                            Duration::Minutes(1)));
    default:
      Crash(absl::StrCat("Unsupported Fuzzing Action of type: ",
                         action.type_case()));
  }
  return BasicFuzzer::Result::kComplete;
}

void BasicFuzzer::TryShutdown() {
  engine()->FuzzingDone();
  if (channel() != nullptr) {
    DestroyChannel();
  }
  if (server() != nullptr) {
    if (!server_shutdown_called()) {
      ShutdownServer();
    }
    if (server_finished_shutting_down()) {
      DestroyServer();
    }
  }
  ShutdownCalls();

  grpc_timer_manager_tick();
  GPR_ASSERT(PollCq() == Result::kPending);
}

void BasicFuzzer::Run(absl::Span<const api_fuzzer::Action* const> actions) {
  size_t action_index = 0;
  auto allow_forced_shutdown = std::make_shared<bool>(false);
  auto no_more_actions = [&]() { action_index = actions.size(); };

  engine()->RunAfterExactly(minimum_run_time_, [allow_forced_shutdown] {
    *allow_forced_shutdown = true;
  });

  while (action_index < actions.size() || Continue()) {
    Tick();

    if (paused_) continue;

    if (action_index == actions.size()) {
      if (*allow_forced_shutdown) TryShutdown();
      continue;
    }

    auto result = ExecuteAction(*actions[action_index++]);
    if (result == Result::kFailed) {
      no_more_actions();
    }
  }
}

}  // namespace testing
}  // namespace grpc_core
