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

#include "transport.h"

namespace grpc_core {

namespace {
void* Offset(void* base, size_t amt) { return static_cast<char*>(base) + amt; }
}  // namespace

namespace filters_detail {

template <typename T>
PipeTransformer<T>::~PipeTransformer() {
  if (promise_data_ != nullptr) {
    ops_->early_destroy(promise_data_);
    gpr_free_aligned(promise_data_);
  }
}

template <typename T>
Poll<ResultOr<T>> PipeTransformer<T>::Start(
    const Layout<FallibleOperator<T>>* layout, T input, void* call_data) {
  ops_ = layout->ops.data();
  end_ops_ = ops_ + layout->ops.size();
  if (layout->promise_size == 0) {
    // No call state ==> instantaneously ready
    auto r = InitStep(std::move(input), call_data);
    GPR_ASSERT(r.ready());
    return r;
  }
  promise_data_ =
      gpr_malloc_aligned(layout->promise_size, layout->promise_alignment);
  return InitStep(std::move(input), call_data);
}

template <typename T>
Poll<ResultOr<T>> PipeTransformer<T>::InitStep(T input, void* call_data) {
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
Poll<ResultOr<T>> PipeTransformer<T>::Step(void* call_data) {
  GPR_DEBUG_ASSERT(promise_data_ != nullptr);
  auto p = ContinueStep(call_data);
  if (p.ready()) {
    gpr_free_aligned(promise_data_);
    promise_data_ = nullptr;
  }
  return p;
}

template <typename T>
Poll<ResultOr<T>> PipeTransformer<T>::ContinueStep(void* call_data) {
  auto p = ops_->poll(promise_data_);
  if (auto* r = p.value_if_ready()) {
    if (r->ok == nullptr) return std::move(*r);
    return InitStep(std::move(r->ok), call_data);
  }
  return Pending{};
}

template <typename T>
InfalliblePipeTransformer<T>::~InfalliblePipeTransformer() {
  if (promise_data_ != nullptr) {
    ops_->early_destroy(promise_data_);
    gpr_free_aligned(promise_data_);
  }
}

template <typename T>
Poll<T> InfalliblePipeTransformer<T>::Start(
    const Layout<InfallibleOperator<T>>* layout, T input, void* call_data) {
  ops_ = layout->ops.data();
  end_ops_ = ops_ + layout->ops.size();
  if (layout->promise_size == 0) {
    // No call state ==> instantaneously ready
    auto r = InitStep(std::move(input), call_data);
    GPR_ASSERT(r.ready());
    return r;
  }
  promise_data_ =
      gpr_malloc_aligned(layout->promise_size, layout->promise_alignment);
  return InitStep(std::move(input), call_data);
}

template <typename T>
Poll<T> InfalliblePipeTransformer<T>::InitStep(T input, void* call_data) {
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
Poll<T> InfalliblePipeTransformer<T>::Step(void* call_data) {
  GPR_DEBUG_ASSERT(promise_data_ != nullptr);
  auto p = ContinueStep(call_data);
  if (p.ready()) {
    gpr_free_aligned(promise_data_);
    promise_data_ = nullptr;
  }
  return p;
}

template <typename T>
Poll<T> InfalliblePipeTransformer<T>::ContinueStep(void* call_data) {
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
template class PipeTransformer<ClientMetadataHandle>;
template class PipeTransformer<MessageHandle>;
template class InfalliblePipeTransformer<ServerMetadataHandle>;
}  // namespace filters_detail

///////////////////////////////////////////////////////////////////////////////
// CallFilters

CallFilters::CallFilters() : stack_(nullptr), call_data_(nullptr) {}

CallFilters::CallFilters(RefCountedPtr<Stack> stack)
    : stack_(std::move(stack)),
      call_data_(gpr_malloc_aligned(stack->data_.call_data_size,
                                    stack->data_.call_data_alignment)) {}

CallFilters::~CallFilters() {
  if (call_data_ != nullptr) gpr_free_aligned(call_data_);
}

void CallFilters::SetStack(RefCountedPtr<Stack> stack) {
  if (call_data_ != nullptr) gpr_free_aligned(call_data_);
  stack_ = std::move(stack);
  call_data_ = gpr_malloc_aligned(stack->data_.call_data_size,
                                  stack->data_.call_data_alignment);
  stack_waiter_.Wake();
}

void CallFilters::Finalize(const grpc_call_final_info* final_info) {
  for (auto& finalizer : stack_->data_.finalizers) {
    finalizer.final(Offset(call_data_, finalizer.call_offset),
                    finalizer.channel_data, final_info);
  }
}

void CallFilters::CancelDueToFailedPipeOperation() {
  // We expect something cancelled before now
  if (server_trailing_metadata_ == nullptr) return;
  gpr_log(GPR_DEBUG, "Cancelling due to failed pipe operation");
  server_trailing_metadata_ =
      ServerMetadataFromStatus(absl::CancelledError("Failed pipe operation"));
  server_trailing_metadata_waiter_.Wake();
}

///////////////////////////////////////////////////////////////////////////////
// CallFilters::StackBuilder

size_t CallFilters::StackBuilder::OffsetForNextFilter(size_t alignment,
                                                      size_t size) {
  data_.call_data_alignment = std::max(data_.call_data_alignment, alignment);
  if (data_.call_data_size % alignment != 0) {
    data_.call_data_size += alignment - data_.call_data_size % alignment;
  }
  const size_t offset = data_.call_data_size;
  data_.call_data_size += size;
  return offset;
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

void CallFilters::PipeState::BeginPush() {
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

void CallFilters::PipeState::AbandonPush() {
  switch (state_) {
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
    case ValueState::kWaiting:
      state_ = ValueState::kError;
      wait_recv_.Wake();
      break;
    case ValueState::kIdle:
    case ValueState::kClosed:
    case ValueState::kError:
      break;
  }
}

Poll<StatusFlag> CallFilters::PipeState::PollPush() {
  switch (state_) {
    case ValueState::kIdle:
    // Read completed and new read started => we see waiting here
    case ValueState::kWaiting:
    case ValueState::kClosed:
      return Success{};
    case ValueState::kQueued:
    case ValueState::kReady:
    case ValueState::kProcessing:
      return wait_send_.pending();
    case ValueState::kError:
      return Failure{};
  }
}

Poll<StatusFlag> CallFilters::PipeState::PollPullValue() {
  switch (state_) {
    case ValueState::kIdle:
      state_ = ValueState::kWaiting;
      return wait_recv_.pending();
    case ValueState::kReady:
    case ValueState::kQueued:
      state_ = ValueState::kProcessing;
      return Success{};
    case ValueState::kProcessing:
    case ValueState::kWaiting:
      Crash("Only one pull allowed to be outstanding");
    case ValueState::kClosed:
    case ValueState::kError:
      return Failure{};
  }
}

void CallFilters::PipeState::AckPullValue() {
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

}  // namespace grpc_core
