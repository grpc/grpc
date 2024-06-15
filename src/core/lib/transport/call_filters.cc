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
      destructor.call_destroy(
          filters_detail::Offset(call_data_, destructor.call_offset));
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
    constructor.call_init(
        filters_detail::Offset(call_data_, constructor.call_offset),
        constructor.channel_data);
  }
  call_state_.Start();
}

void CallFilters::Finalize(const grpc_call_final_info* final_info) {
  for (auto& finalizer : stack_->data_.finalizers) {
    finalizer.final(filters_detail::Offset(call_data_, finalizer.call_offset),
                    finalizer.channel_data, final_info);
  }
}

void CallFilters::CancelDueToFailedPipeOperation(SourceLocation but_where) {
  // We expect something cancelled before now
  if (push_server_trailing_metadata_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(promise_primitives)) {
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
  if (GRPC_TRACE_FLAG_ENABLED(call)) {
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

std::string CallState::DebugString() const {
  return absl::StrCat(
      "client_to_server_pull_state:", client_to_server_pull_state_,
      " client_to_server_push_state:", client_to_server_push_state_,
      " server_to_client_pull_state:", server_to_client_pull_state_,
      " server_to_client_message_push_state:", server_to_client_push_state_,
      " server_trailing_metadata_state:", server_trailing_metadata_state_,
      client_to_server_push_waiter_.DebugString(),
      " server_to_client_push_waiter:",
      server_to_client_push_waiter_.DebugString(),
      " client_to_server_pull_waiter:",
      client_to_server_pull_waiter_.DebugString(),
      " server_to_client_pull_waiter:",
      server_to_client_pull_waiter_.DebugString(),
      " server_trailing_metadata_waiter:",
      server_trailing_metadata_waiter_.DebugString());
}

static_assert(sizeof(CallState) <= 16, "CallState too large");

}  // namespace filters_detail
}  // namespace grpc_core
