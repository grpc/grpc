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
#include "absl/log/log.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

namespace filters_detail {

void RunHalfClose(absl::Span<const HalfCloseOperator> ops, void* call_data) {
  for (const auto& op : ops) {
    op.half_close(Offset(call_data, op.call_offset), op.channel_data);
  }
}

ServerMetadataHandle RunServerTrailingMetadata(
    absl::Span<const ServerTrailingMetadataOperator> ops, void* call_data,
    ServerMetadataHandle md) {
  for (auto& op : ops) {
    md = op.server_trailing_metadata(Offset(call_data, op.call_offset),
                                     op.channel_data, std::move(md));
  }
  return md;
}

template <typename T>
OperationExecutor<T>::~OperationExecutor() {
  if (promise_data_ != nullptr) {
    ops_->early_destroy(promise_data_);
    gpr_free_aligned(promise_data_);
  }
}

template <typename T>
Poll<ResultOr<T>> OperationExecutor<T>::Start(const Layout<T>* layout, T input,
                                              void* call_data) {
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

// Explicit instantiations of some types used in filters.h
// We'll need to add ServerMetadataHandle to this when it becomes different
// to ClientMetadataHandle
template class OperationExecutor<ClientMetadataHandle>;
template class OperationExecutor<MessageHandle>;

}  // namespace filters_detail

namespace {
// Call data for those calls that don't have any call data
// (we form pointers to this that aren't allowed to be nullptr)
char g_empty_call_data;
}  // namespace

///////////////////////////////////////////////////////////////////////////////
// CallFilters

CallFilters::CallFilters(ClientMetadataHandle client_initial_metadata)
    : call_data_(nullptr),
      push_client_initial_metadata_(std::move(client_initial_metadata)) {}

CallFilters::~CallFilters() {
  if (call_data_ != nullptr && call_data_ != &g_empty_call_data) {
    for (const auto& stack : stacks_) {
      for (const auto& destructor : stack.stack->data_.filter_destructor) {
        destructor.call_destroy(filters_detail::Offset(
            call_data_, stack.call_data_offset + destructor.call_offset));
      }
    }
    gpr_free_aligned(call_data_);
  }
}

void CallFilters::Start() {
  CHECK_EQ(call_data_, nullptr);
  size_t call_data_alignment = 1;
  for (const auto& stack : stacks_) {
    call_data_alignment =
        std::max(call_data_alignment, stack.stack->data_.call_data_alignment);
  }
  size_t call_data_size = 0;
  for (auto& stack : stacks_) {
    stack.call_data_offset = call_data_size;
    size_t stack_call_data_size = stack.stack->data_.call_data_size;
    if (stack_call_data_size % call_data_alignment != 0) {
      stack_call_data_size +=
          call_data_alignment - stack_call_data_size % call_data_alignment;
    }
    call_data_size += stack_call_data_size;
  }
  if (call_data_size != 0) {
    call_data_ = gpr_malloc_aligned(call_data_size, call_data_alignment);
  } else {
    call_data_ = &g_empty_call_data;
  }
  for (const auto& stack : stacks_) {
    for (const auto& constructor : stack.stack->data_.filter_constructor) {
      constructor.call_init(
          filters_detail::Offset(
              call_data_, stack.call_data_offset + constructor.call_offset),
          constructor.channel_data);
    }
  }
  call_state_.Start();
}

void CallFilters::Finalize(const grpc_call_final_info* final_info) {
  for (auto& stack : stacks_) {
    for (auto& finalizer : stack.stack->data_.finalizers) {
      finalizer.final(
          filters_detail::Offset(
              call_data_, stack.call_data_offset + finalizer.call_offset),
          finalizer.channel_data, final_info);
    }
  }
}

void CallFilters::CancelDueToFailedPipeOperation(SourceLocation but_where) {
  // We expect something cancelled before now
  if (push_server_trailing_metadata_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(promise_primitives)) {
    VLOG(2).AtLocation(but_where.file(), but_where.line())
        << "Cancelling due to failed pipe operation: " << DebugString();
  }
  auto status =
      ServerMetadataFromStatus(GRPC_STATUS_CANCELLED, "Failed pipe operation");
  status->Set(GrpcCallWasCancelled(), true);
  PushServerTrailingMetadata(std::move(status));
}

void CallFilters::PushServerTrailingMetadata(ServerMetadataHandle md) {
  CHECK(md != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(call)) {
    LOG(INFO) << GetContext<Activity>()->DebugTag()
              << " PushServerTrailingMetadata[" << this
              << "]: " << md->DebugString() << " into " << DebugString();
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
  absl::c_reverse(data_.server_trailing_metadata);
  return RefCountedPtr<Stack>(new Stack(std::move(data_)));
}

}  // namespace grpc_core
