// Copyright 2024 gRPC authors.
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

#include "src/core/lib/transport/call_filters.h"

#include "absl/log/check.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

namespace {
void* Offset(void* base, size_t amt) { return static_cast<char*>(base) + amt; }
}  // namespace

namespace filters_detail {

void RunHalfClose(absl::Span<const HalfCloseOperator> ops, void* call_data) {
  for (const auto& op : ops) {
    op.half_close(Offset(call_data, op.call_offset), op.channel_data);
  }
}

template <typename T>
OperationExecutor<T>::~OperationExecutor() {
  if (promise_data_ != nullptr) {
    ops_->early_destroy(promise_data_);
    gpr_free_aligned(promise_data_);
  }
}

template <typename T>
Poll<ResultOr<T>> OperationExecutor<T>::Start(
    const Layout<FallibleOperator<T>>* layout, T input, void* call_data) {
  ops_ = layout->ops.data();
  end_ops_ = ops_ + layout->ops.size();
  if (layout->promise_size == 0) {
    // No call state ==> instantaneously ready
    auto r = InitStep(std::move(input), call_data);
    CHECK(r.ready());
    return r;
  }
  promise_data_ =
      gpr_malloc_aligned(layout->promise_size, layout->promise_alignment);
  return InitStep(std::move(input), call_data);
}

template <typename T>
Poll<ResultOr<T>> OperationExecutor<T>::InitStep(T input, void* call_data) {
  CHECK(input != nullptr);
  while (true) {
    if (ops_ == end_ops_) {
      return ResultOr<T>{std::move(input), nullptr};
    }
    auto p =
        ops_->promise_init(promise_data_, Offset(call_data, ops_->call_offset),
                           ops_->channel_data, std::move(input));
    if (auto* r = p.value_if_ready()) {
      if (r->ok == nullptr) return std::move(*r);
      input = std::move(r->ok);
      ++ops_;
      continue;
    }
    return Pending{};
  }
}

template <typename T>
Poll<ResultOr<T>> OperationExecutor<T>::Step(void* call_data) {
  DCHECK_NE(promise_data_, nullptr);
  auto p = ContinueStep(call_data);
  if (p.ready()) {
    gpr_free_aligned(promise_data_);
    promise_data_ = nullptr;
  }
  return p;
}

template <typename T>
Poll<ResultOr<T>> OperationExecutor<T>::ContinueStep(void* call_data) {
  auto p = ops_->poll(promise_data_);
  if (auto* r = p.value_if_ready()) {
    if (r->ok == nullptr) return std::move(*r);
    ++ops_;
    return InitStep(std::move(r->ok), call_data);
  }
  return Pending{};
}

template <typename T>
InfallibleOperationExecutor<T>::~InfallibleOperationExecutor() {
  if (promise_data_ != nullptr) {
    ops_->early_destroy(promise_data_);
    gpr_free_aligned(promise_data_);
  }
}

template <typename T>
Poll<T> InfallibleOperationExecutor<T>::Start(
    const Layout<InfallibleOperator<T>>* layout, T input, void* call_data) {
  ops_ = layout->ops.data();
  end_ops_ = ops_ + layout->ops.size();
  if (layout->promise_size == 0) {
    // No call state ==> instantaneously ready
    auto r = InitStep(std::move(input), call_data);
    CHECK(r.ready());
    return r;
  }
  promise_data_ =
      gpr_malloc_aligned(layout->promise_size, layout->promise_alignment);
  return InitStep(std::move(input), call_data);
}

template <typename T>
Poll<T> InfallibleOperationExecutor<T>::InitStep(T input, void* call_data) {
  while (true) {
    if (ops_ == end_ops_) {
      return input;
    }
    auto p =
        ops_->promise_init(promise_data_, Offset(call_data, ops_->call_offset),
                           ops_->channel_data, std::move(input));
    if (auto* r = p.value_if_ready()) {
      input = std::move(*r);
      ++ops_;
      continue;
    }
    return Pending{};
  }
}

template <typename T>
Poll<T> InfallibleOperationExecutor<T>::Step(void* call_data) {
  DCHECK_NE(promise_data_, nullptr);
  auto p = ContinueStep(call_data);
  if (p.ready()) {
    gpr_free_aligned(promise_data_);
    promise_data_ = nullptr;
  }
  return p;
}

template <typename T>
Poll<T> InfallibleOperationExecutor<T>::ContinueStep(void* call_data) {
  auto p = ops_->poll(promise_data_);
  if (auto* r = p.value_if_ready()) {
    ++ops_;
    return InitStep(std::move(*r), call_data);
  }
  return Pending{};
}

// Explicit instantiations of some types used in filters.h
// We'll need to add ServerMetadataHandle to this when it becomes different
// to ClientMetadataHandle
template class OperationExecutor<ClientMetadataHandle>;
template class OperationExecutor<MessageHandle>;
template class InfallibleOperationExecutor<ServerMetadataHandle>;

}  // namespace filters_detail

namespace {
// Call data for those calls that don't have any call data
// (we form pointers to this that aren't allowed to be nullptr)
char g_empty_call_data;
}  // namespace

///////////////////////////////////////////////////////////////////////////////
// CallFilters

CallFilters::CallFilters(ClientMetadataHandle client_initial_metadata)
    : stack_(nullptr),
      call_data_(nullptr),
      client_initial_metadata_(std::move(client_initial_metadata)) {}

CallFilters::~CallFilters() {
  if (call_data_ != nullptr && call_data_ != &g_empty_call_data) {
    for (const auto& destructor : stack_->data_.filter_destructor) {
      destructor.call_destroy(Offset(call_data_, destructor.call_offset));
    }
    gpr_free_aligned(call_data_);
  }
}

void CallFilters::SetStack(RefCountedPtr<Stack> stack) {
  CHECK_EQ(call_data_, nullptr);
  stack_ = std::move(stack);
  if (stack_->data_.call_data_size != 0) {
    call_data_ = gpr_malloc_aligned(stack_->data_.call_data_size,
                                    stack_->data_.call_data_alignment);
  } else {
    call_data_ = &g_empty_call_data;
  }
  for (const auto& constructor : stack_->data_.filter_constructor) {
    constructor.call_init(Offset(call_data_, constructor.call_offset),
                          constructor.channel_data);
  }
  client_initial_metadata_state_.Start();
  client_to_server_message_state_.Start();
  server_initial_metadata_state_.Start();
  server_to_client_message_state_.Start();
}

void CallFilters::Finalize(const grpc_call_final_info* final_info) {
  for (auto& finalizer : stack_->data_.finalizers) {
    finalizer.final(Offset(call_data_, finalizer.call_offset),
                    finalizer.channel_data, final_info);
  }
}

void CallFilters::CancelDueToFailedPipeOperation(SourceLocation but_where) {
  // We expect something cancelled before now
  if (server_trailing_metadata_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(promise_primitives)) {
    gpr_log(but_where.file(), but_where.line(), GPR_LOG_SEVERITY_DEBUG,
            "Cancelling due to failed pipe operation: %s",
            DebugString().c_str());
  }
  PushServerTrailingMetadata(
      ServerMetadataFromStatus(absl::CancelledError("Failed pipe operation")));
  server_trailing_metadata_waiter_.Wake();
}

void CallFilters::PushServerTrailingMetadata(ServerMetadataHandle md) {
  CHECK(md != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(promise_primitives)) {
    gpr_log(GPR_INFO, "%s PushServerTrailingMetadata[%p]: %s into %s",
            GetContext<Activity>()->DebugTag().c_str(), this,
            md->DebugString().c_str(), DebugString().c_str());
  }
  CHECK(md != nullptr);
  if (cancelled_.is_set()) return;
  cancelled_.Set(md->get(GrpcCallWasCancelled()).value_or(false));
  server_trailing_metadata_ = std::move(md);
  client_initial_metadata_state_.CloseWithError();
  server_initial_metadata_state_.CloseSending();
  client_to_server_message_state_.CloseWithError();
  server_to_client_message_state_.CloseSending();
  server_trailing_metadata_waiter_.Wake();
}

std::string CallFilters::DebugString() const {
  std::vector<std::string> components = {
      absl::StrFormat("this:%p", this),
      absl::StrCat("client_initial_metadata:",
                   client_initial_metadata_state_.DebugString()),
      ServerInitialMetadataPromises::DebugString("server_initial_metadata",
                                                 this),
      ClientToServerMessagePromises::DebugString("client_to_server_message",
                                                 this),
      ServerToClientMessagePromises::DebugString("server_to_client_message",
                                                 this),
      absl::StrCat("server_trailing_metadata:",
                   server_trailing_metadata_ == nullptr
                       ? "not-set"
                       : server_trailing_metadata_->DebugString())};
  return absl::StrCat("CallFilters{", absl::StrJoin(components, ", "), "}");
};

///////////////////////////////////////////////////////////////////////////////
// CallFilters::Stack

CallFilters::Stack::~Stack() {
  for (auto& destructor : data_.channel_data_destructors) {
    destructor.destroy(destructor.channel_data);
  }
}

///////////////////////////////////////////////////////////////////////////////
// CallFilters::StackBuilder

CallFilters::StackBuilder::~StackBuilder() {
  for (auto& destructor : data_.channel_data_destructors) {
    destructor.destroy(destructor.channel_data);
  }
}

RefCountedPtr<CallFilters::Stack> CallFilters::StackBuilder::Build() {
  if (data_.call_data_size % data_.call_data_alignment != 0) {
    data_.call_data_size += data_.call_data_alignment -
                            data_.call_data_size % data_.call_data_alignment;
  }
  // server -> client needs to be reversed so that we can iterate all stacks
  // in the same order
  data_.server_initial_metadata.Reverse();
  data_.server_to_client_messages.Reverse();
  data_.server_trailing_metadata.Reverse();
  return RefCountedPtr<Stack>(new Stack(std::move(data_)));
}

///////////////////////////////////////////////////////////////////////////////
// CallFilters::PipeState

void filters_detail::PipeState::Start() {
  DCHECK(!started_);
  started_ = true;
  wait_recv_.Wake();
}

void filters_detail::PipeState::CloseWithError() {
  if (state_ == ValueState::kClosed) return;
  state_ = ValueState::kError;
  wait_recv_.Wake();
  wait_send_.Wake();
}

Poll<bool> filters_detail::PipeState::PollClosed() {
  switch (state_) {
    case ValueState::kIdle:
    case ValueState::kWaiting:
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
      return wait_recv_.pending();
    case ValueState::kClosed:
      return false;
    case ValueState::kError:
      return true;
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

void filters_detail::PipeState::CloseSending() {
  switch (state_) {
    case ValueState::kIdle:
      state_ = ValueState::kClosed;
      break;
    case ValueState::kWaiting:
      state_ = ValueState::kClosed;
      wait_recv_.Wake();
      break;
    case ValueState::kClosed:
    case ValueState::kError:
      break;
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
      Crash("Only one push allowed to be outstanding");
      break;
  }
}

void filters_detail::PipeState::BeginPush() {
  switch (state_) {
    case ValueState::kIdle:
      state_ = ValueState::kQueued;
      break;
    case ValueState::kWaiting:
      state_ = ValueState::kReady;
      wait_recv_.Wake();
      break;
    case ValueState::kClosed:
    case ValueState::kError:
      break;
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
      Crash("Only one push allowed to be outstanding");
      break;
  }
}

void filters_detail::PipeState::DropPush() {
  switch (state_) {
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
    case ValueState::kWaiting:
      if (GRPC_TRACE_FLAG_ENABLED(promise_primitives)) {
        gpr_log(GPR_INFO, "%p drop push in state %s", this,
                DebugString().c_str());
      }
      state_ = ValueState::kError;
      wait_recv_.Wake();
      break;
    case ValueState::kIdle:
    case ValueState::kClosed:
    case ValueState::kError:
      break;
  }
}

void filters_detail::PipeState::DropPull() {
  switch (state_) {
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
    case ValueState::kWaiting:
      if (GRPC_TRACE_FLAG_ENABLED(promise_primitives)) {
        gpr_log(GPR_INFO, "%p drop pull in state %s", this,
                DebugString().c_str());
      }
      state_ = ValueState::kError;
      wait_send_.Wake();
      break;
    case ValueState::kIdle:
    case ValueState::kClosed:
    case ValueState::kError:
      break;
  }
}

Poll<StatusFlag> filters_detail::PipeState::PollPush() {
  switch (state_) {
    // Read completed and new read started => we see waiting here
    case ValueState::kWaiting:
      state_ = ValueState::kReady;
      wait_recv_.Wake();
      return wait_send_.pending();
    case ValueState::kIdle:
    case ValueState::kClosed:
      return Success{};
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
      return wait_send_.pending();
    case ValueState::kError:
      return Failure{};
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

Poll<ValueOrFailure<bool>> filters_detail::PipeState::PollPull() {
  switch (state_) {
    case ValueState::kWaiting:
      return wait_recv_.pending();
    case ValueState::kIdle:
      state_ = ValueState::kWaiting;
      return wait_recv_.pending();
    case ValueState::kReady:
    case ValueState::kQueued:
      if (!started_) return wait_recv_.pending();
      state_ = ValueState::kProcessing;
      return true;
    case ValueState::kProcessing:
      Crash("Only one pull allowed to be outstanding");
    case ValueState::kClosed:
      return false;
    case ValueState::kError:
      return Failure{};
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

void filters_detail::PipeState::AckPull() {
  switch (state_) {
    case ValueState::kProcessing:
      state_ = ValueState::kIdle;
      wait_send_.Wake();
      break;
    case ValueState::kWaiting:
    case ValueState::kIdle:
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kClosed:
      Crash("AckPullValue called in invalid state");
    case ValueState::kError:
      break;
  }
}

std::string filters_detail::PipeState::DebugString() const {
  const char* state_str = "<<invalid-value>>";
  switch (state_) {
    case ValueState::kIdle:
      state_str = "Idle";
      break;
    case ValueState::kWaiting:
      state_str = "Waiting";
      break;
    case ValueState::kQueued:
      state_str = "Queued";
      break;
    case ValueState::kReady:
      state_str = "Ready";
      break;
    case ValueState::kProcessing:
      state_str = "Processing";
      break;
    case ValueState::kClosed:
      state_str = "Closed";
      break;
    case ValueState::kError:
      state_str = "Error";
      break;
  }
  return absl::StrCat(state_str, started_ ? "" : " (not started)");
}

}  // namespace grpc_core
