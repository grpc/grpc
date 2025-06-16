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

#include "src/core/call/call_filters.h"

#include <grpc/support/port_platform.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/call/metadata.h"
#include "src/core/util/crash.h"

namespace grpc_core {
// Call data for those calls that don't have any call data
// (we form pointers to this that aren't allowed to be nullptr)
char CallFilters::g_empty_call_data_;

///////////////////////////////////////////////////////////////////////////////
// CallFilters

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
    call_data_ = &g_empty_call_data_;
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
  GRPC_TRACE_VLOG(promise_primitives, 2)
          .AtLocation(but_where.file(), but_where.line())
      << "Cancelling due to failed pipe operation: " << DebugString();
  Cancel();
}

void CallFilters::PushServerTrailingMetadata(ServerMetadataHandle md) {
  CHECK(md != nullptr);
  GRPC_TRACE_LOG(call, INFO)
      << GetContext<Activity>()->DebugTag() << " PushServerTrailingMetadata["
      << this << "]: " << md->DebugString() << " into " << DebugString();
  CHECK(md != nullptr);
  if (call_state_.PushServerTrailingMetadata(
          md->get(GrpcCallWasCancelled()).value_or(false))) {
    push_server_trailing_metadata_ = std::move(md);
  }
}

void CallFilters::Cancel() {
  GRPC_TRACE_LOG(call, INFO) << GetContext<Activity>()->DebugTag() << " Cancel["
                             << this << "]: into " << DebugString();
  if (call_state_.PushServerTrailingMetadata(true)) {
    push_server_trailing_metadata_ =
        CancelledServerMetadataFromStatus(GRPC_STATUS_CANCELLED);
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
