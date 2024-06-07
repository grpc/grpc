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
      push_client_initial_metadata_(std::move(client_initial_metadata)) {}

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
  call_state_.Start();
}

void CallFilters::Finalize(const grpc_call_final_info* final_info) {
  for (auto& finalizer : stack_->data_.finalizers) {
    finalizer.final(Offset(call_data_, finalizer.call_offset),
                    finalizer.channel_data, final_info);
  }
}

void CallFilters::CancelDueToFailedPipeOperation(SourceLocation but_where) {
  // We expect something cancelled before now
  if (push_server_trailing_metadata_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
    gpr_log(but_where.file(), but_where.line(), GPR_LOG_SEVERITY_DEBUG,
            "Cancelling due to failed pipe operation: %s",
            DebugString().c_str());
  }
  auto status =
      ServerMetadataFromStatus(absl::CancelledError("Failed pipe operation"));
  status->Set(GrpcCallWasCancelled(), true);
  PushServerTrailingMetadata(std::move(status));
}

void CallFilters::PushServerTrailingMetadata(ServerMetadataHandle md) {
  CHECK(md != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
    gpr_log(GPR_INFO, "%s PushServerTrailingMetadata[%p]: %s into %s",
            GetContext<Activity>()->DebugTag().c_str(), this,
            md->DebugString().c_str(), DebugString().c_str());
  }
  CHECK(md != nullptr);
  if (call_state_.PushServerTrailingMetadata(
          md->get(GrpcCallWasCancelled()).value_or(false))) {
    push_server_trailing_metadata_ = std::move(md);
  }
}

std::string CallFilters::DebugString() const {
  std::vector<std::string> components = {
      absl::StrFormat("this:%p", this),
      absl::StrCat("state:", call_state_.DebugString()),
      absl::StrCat("server_trailing_metadata:",
                   push_server_trailing_metadata_ == nullptr
                       ? "not-set"
                       : push_server_trailing_metadata_->DebugString())};
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
// CallState

namespace filters_detail {

CallState::CallState()
    : client_to_server_pull_state_(ClientToServerPullState::kBegin),
      client_to_server_push_state_(ClientToServerPushState::kIdle),
      server_to_client_pull_state_(ServerToClientPullState::kUnstarted),
      pushed_server_initial_metadata_(false),
      server_to_client_message_push_state_(
          ServerToClientMessagePushState::kIdle),
      pushed_server_trailing_metadata_(false) {}

void CallState::Start() {
  switch (server_to_client_pull_state_) {
    case ServerToClientPullState::kUnstarted:
      server_to_client_pull_state_ = ServerToClientPullState::kStarted;
      server_to_client_pull_waiter_.Wake();
      break;
    case ServerToClientPullState::kStarted:
    case ServerToClientPullState::kProcessingServerInitialMetadata:
    case ServerToClientPullState::kIdle:
    case ServerToClientPullState::kReading:
    case ServerToClientPullState::kProcessingServerToClientMessage:
    case ServerToClientPullState::kProcessingServerTrailingMetadata:
      Crash("Start called twice");
    case ServerToClientPullState::kTerminated:
      break;
  }
}

void CallState::BeginPushClientToServerMessage() {
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      client_to_server_push_state_ = ClientToServerPushState::kPushedMessage;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      Crash("PushClientToServerMessage called twice concurrently");
      break;
    case ClientToServerPushState::kPushedHalfClose:
      Crash("PushClientToServerMessage called after half-close");
      break;
    case ClientToServerPushState::kFinished:
      break;
  }
}

Poll<StatusFlag> CallState::PollPushClientToServerMessage() {
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
    case ClientToServerPushState::kPushedHalfClose:
      return Success{};
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      return client_to_server_push_waiter_.pending();
    case ClientToServerPushState::kFinished:
      return Failure{};
  }
}

void CallState::ClientToServerHalfClose() {
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      client_to_server_push_state_ = ClientToServerPushState::kPushedHalfClose;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedMessage:
      client_to_server_push_state_ =
          ClientToServerPushState::kPushedMessageAndHalfClosed;
      break;
    case ClientToServerPushState::kPushedHalfClose:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      Crash("ClientToServerHalfClose called twice");
      break;
    case ClientToServerPushState::kFinished:
      break;
  }
}

void CallState::BeginPullClientInitialMetadata() {
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
      client_to_server_pull_state_ =
          ClientToServerPullState::kProcessingClientInitialMetadata;
      break;
    case ClientToServerPullState::kProcessingClientInitialMetadata:
    case ClientToServerPullState::kIdle:
    case ClientToServerPullState::kReading:
    case ClientToServerPullState::kProcessingClientToServerMessage:
      Crash("BeginPullClientInitialMetadata called twice");
      break;
    case ClientToServerPullState::kTerminated:
      break;
  }
}

void CallState::FinishPullClientInitialMetadata() {
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
      Crash("FinishPullClientInitialMetadata called before Begin");
      break;
    case ClientToServerPullState::kProcessingClientInitialMetadata:
      client_to_server_pull_state_ = ClientToServerPullState::kIdle;
      client_to_server_pull_waiter_.Wake();
      break;
    case ClientToServerPullState::kIdle:
    case ClientToServerPullState::kReading:
    case ClientToServerPullState::kProcessingClientToServerMessage:
      Crash("Out of order FinishPullClientInitialMetadata");
      break;
    case ClientToServerPullState::kTerminated:
      break;
  }
}

Poll<StatusFlag> CallState::PollPullClientToServerMessageAvailable() {
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
    case ClientToServerPullState::kProcessingClientInitialMetadata:
      return client_to_server_pull_waiter_.pending();
    case ClientToServerPullState::kIdle:
      client_to_server_pull_state_ = ClientToServerPullState::kReading;
      ABSL_FALLTHROUGH_INTENDED;
    case ClientToServerPullState::kReading:
      break;
    case ClientToServerPullState::kProcessingClientToServerMessage:
      Crash(
          "PollPullClientToServerMessageAvailable called while processing a "
          "message");
      break;
    case ClientToServerPullState::kTerminated:
      return Failure{};
  }
  DCHECK_EQ(client_to_server_pull_state_, ClientToServerPullState::kReading);
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      return client_to_server_push_waiter_.pending();
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      client_to_server_pull_state_ =
          ClientToServerPullState::kProcessingClientToServerMessage;
      return Success{};
    case ClientToServerPushState::kPushedHalfClose:
      return Success{};
    case ClientToServerPushState::kFinished:
      client_to_server_pull_state_ = ClientToServerPullState::kTerminated;
      return Failure{};
  }
}

void CallState::FinishPullClientToServerMessage() {
  switch (client_to_server_pull_state_) {
    case ClientToServerPullState::kBegin:
    case ClientToServerPullState::kProcessingClientInitialMetadata:
      Crash("FinishPullClientToServerMessage called before Begin");
      break;
    case ClientToServerPullState::kIdle:
      Crash("FinishPullClientToServerMessage called twice");
      break;
    case ClientToServerPullState::kReading:
      Crash(
          "FinishPullClientToServerMessage called before "
          "PollPullClientToServerMessageAvailable");
      break;
    case ClientToServerPullState::kProcessingClientToServerMessage:
      client_to_server_pull_state_ = ClientToServerPullState::kIdle;
      client_to_server_pull_waiter_.Wake();
      break;
    case ClientToServerPullState::kTerminated:
      break;
  }
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kPushedMessage:
      client_to_server_push_state_ = ClientToServerPushState::kIdle;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kIdle:
    case ClientToServerPushState::kPushedHalfClose:
      Crash("FinishPullClientToServerMessage called without a message");
      break;
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      client_to_server_push_state_ = ClientToServerPushState::kPushedHalfClose;
      break;
    case ClientToServerPushState::kFinished:
      break;
  }
}

void CallState::PushServerInitialMetadata() {
  CHECK(!pushed_server_initial_metadata_);
  pushed_server_initial_metadata_ = true;
  client_to_server_push_waiter_.Wake();
}

void CallState::BeginPushServerToClientMessage() {
  switch (server_to_client_message_push_state_) {
    case ServerToClientMessagePushState::kIdle:
      server_to_client_message_push_state_ =
          ServerToClientMessagePushState::kPushed;
      server_to_client_push_waiter_.Wake();
      break;
    case ServerToClientMessagePushState::kPushed:
      Crash("BeginPushServerToClientMessage called twice concurrently");
      break;
    case ServerToClientMessagePushState::kFinished:
      break;
  }
}

Poll<StatusFlag> CallState::PollPushServerToClientMessage() {
  switch (server_to_client_message_push_state_) {
    case ServerToClientMessagePushState::kIdle:
      return Success{};
    case ServerToClientMessagePushState::kPushed:
      return server_to_client_push_waiter_.pending();
    case ServerToClientMessagePushState::kFinished:
      return Failure{};
  }
}

bool CallState::PushServerTrailingMetadata(bool cancel) {
  if (pushed_server_trailing_metadata_) return false;
  pushed_server_trailing_metadata_ = true;
  pushed_server_initial_metadata_ = true;
  switch (server_to_client_message_push_state_) {
    case ServerToClientMessagePushState::kIdle:
      server_to_client_message_push_state_ =
          ServerToClientMessagePushState::kFinished;
      server_to_client_push_waiter_.Wake();
      break;
    case ServerToClientMessagePushState::kPushed:
      if (cancel) {
        server_to_client_message_push_state_ =
            ServerToClientMessagePushState::kFinished;
        server_to_client_push_waiter_.Wake();
      }
      break;
    case ServerToClientMessagePushState::kFinished:
      break;
  }
  switch (client_to_server_push_state_) {
    case ClientToServerPushState::kIdle:
      client_to_server_push_state_ = ClientToServerPushState::kFinished;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedMessage:
    case ClientToServerPushState::kPushedMessageAndHalfClosed:
      client_to_server_push_state_ = ClientToServerPushState::kFinished;
      client_to_server_push_waiter_.Wake();
      break;
    case ClientToServerPushState::kPushedHalfClose:
    case ClientToServerPushState::kFinished:
      break;
  }
  return true;
}

static_assert(sizeof(CallState) <= 16, "CallState too large");

}  // namespace filters_detail
}  // namespace grpc_core
