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

namespace grpc_core {

namespace filters_detail {

namespace {
void* Offset(void* base, size_t amt) { return static_cast<char*>(base) + amt; }
}  // namespace

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

// Explicit instantiations of some types used in filters.h
// We'll need to add ServerMetadataHandle to this when it becomes different
// to ClientMetadataHandle
template class PipeTransformer<ClientMetadataHandle>;
template class PipeTransformer<MessageHandle>;
}  // namespace filters_detail

///////////////////////////////////////////////////////////////////////////////
// CallFilters::StackBuilder

size_t CallFilters::StackBuilder::OffsetForNextFilter(size_t alignment,
                                                      size_t size) {
  min_alignment_ = std::max(alignment, min_alignment_);
  if (current_call_offset_ % alignment != 0) {
    current_call_offset_ += alignment - current_call_offset_ % alignment;
  }
  const size_t offset = current_call_offset_;
  current_call_offset_ += size;
  return offset;
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
