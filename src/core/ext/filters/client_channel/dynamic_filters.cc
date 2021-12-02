//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/dynamic_filters.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/surface/lame_client.h"

// Conversion between call and call stack.
#define CALL_TO_CALL_STACK(call)                                     \
  (grpc_call_stack*)((char*)(call) + GPR_ROUND_UP_TO_ALIGNMENT_SIZE( \
                                         sizeof(DynamicFilters::Call)))
#define CALL_STACK_TO_CALL(callstack)                     \
  (DynamicFilters::Call*)(((char*)(call_stack)) -         \
                          GPR_ROUND_UP_TO_ALIGNMENT_SIZE( \
                              sizeof(DynamicFilters::Call)))

namespace grpc_core {

//
// DynamicFilters::Call
//

DynamicFilters::Call::Call(Args args, grpc_error_handle* error)
    : channel_stack_(std::move(args.channel_stack)) {
  grpc_call_stack* call_stack = CALL_TO_CALL_STACK(this);
  const grpc_call_element_args call_args = {
      call_stack,        /* call_stack */
      nullptr,           /* server_transport_data */
      args.context,      /* context */
      args.path,         /* path */
      args.start_time,   /* start_time */
      args.deadline,     /* deadline */
      args.arena,        /* arena */
      args.call_combiner /* call_combiner */
  };
  *error = grpc_call_stack_init(channel_stack_->channel_stack_, 1, Destroy,
                                this, &call_args);
  if (GPR_UNLIKELY(*error != GRPC_ERROR_NONE)) {
    gpr_log(GPR_ERROR, "error: %s", grpc_error_std_string(*error).c_str());
    return;
  }
  grpc_call_stack_set_pollset_or_pollset_set(call_stack, args.pollent);
}

void DynamicFilters::Call::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  grpc_call_stack* call_stack = CALL_TO_CALL_STACK(this);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_CALL_LOG_OP(GPR_INFO, top_elem, batch);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
}

void DynamicFilters::Call::SetAfterCallStackDestroy(grpc_closure* closure) {
  GPR_ASSERT(after_call_stack_destroy_ == nullptr);
  GPR_ASSERT(closure != nullptr);
  after_call_stack_destroy_ = closure;
}

RefCountedPtr<DynamicFilters::Call> DynamicFilters::Call::Ref() {
  IncrementRefCount();
  return RefCountedPtr<DynamicFilters::Call>(this);
}

RefCountedPtr<DynamicFilters::Call> DynamicFilters::Call::Ref(
    const DebugLocation& location, const char* reason) {
  IncrementRefCount(location, reason);
  return RefCountedPtr<DynamicFilters::Call>(this);
}

void DynamicFilters::Call::Unref() {
  GRPC_CALL_STACK_UNREF(CALL_TO_CALL_STACK(this), "");
}

void DynamicFilters::Call::Unref(const DebugLocation& /*location*/,
                                 const char* reason) {
  GRPC_CALL_STACK_UNREF(CALL_TO_CALL_STACK(this), reason);
}

void DynamicFilters::Call::Destroy(void* arg, grpc_error_handle /*error*/) {
  DynamicFilters::Call* self = static_cast<DynamicFilters::Call*>(arg);
  // Keep some members before destroying the subchannel call.
  grpc_closure* after_call_stack_destroy = self->after_call_stack_destroy_;
  RefCountedPtr<DynamicFilters> channel_stack = std::move(self->channel_stack_);
  // Destroy the subchannel call.
  self->~Call();
  // Destroy the call stack. This should be after destroying the call, because
  // call->after_call_stack_destroy(), if not null, will free the call arena.
  grpc_call_stack_destroy(CALL_TO_CALL_STACK(self), nullptr,
                          after_call_stack_destroy);
  // Automatically reset channel_stack. This should be after destroying the call
  // stack, because destroying call stack needs access to the channel stack.
}

void DynamicFilters::Call::IncrementRefCount() {
  GRPC_CALL_STACK_REF(CALL_TO_CALL_STACK(this), "");
}

void DynamicFilters::Call::IncrementRefCount(const DebugLocation& /*location*/,
                                             const char* reason) {
  GRPC_CALL_STACK_REF(CALL_TO_CALL_STACK(this), reason);
}

//
// DynamicFilters
//

namespace {

void DestroyChannelStack(void* arg, grpc_error_handle /*error*/) {
  grpc_channel_stack* channel_stack = static_cast<grpc_channel_stack*>(arg);
  grpc_channel_stack_destroy(channel_stack);
  gpr_free(channel_stack);
}

std::pair<grpc_channel_stack*, grpc_error_handle> CreateChannelStack(
    const grpc_channel_args* args,
    std::vector<const grpc_channel_filter*> filters) {
  // Allocate memory for channel stack.
  const size_t channel_stack_size =
      grpc_channel_stack_size(filters.data(), filters.size());
  grpc_channel_stack* channel_stack =
      reinterpret_cast<grpc_channel_stack*>(gpr_zalloc(channel_stack_size));
  // Initialize stack.
  grpc_error_handle error = grpc_channel_stack_init(
      /*initial_refs=*/1, DestroyChannelStack, channel_stack, filters.data(),
      filters.size(), args, /*optional_transport=*/nullptr, "DynamicFilters",
      channel_stack);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "error initializing client internal stack: %s",
            grpc_error_std_string(error).c_str());
    grpc_channel_stack_destroy(channel_stack);
    gpr_free(channel_stack);
    return {nullptr, error};
  }
  return {channel_stack, GRPC_ERROR_NONE};
}

}  // namespace

RefCountedPtr<DynamicFilters> DynamicFilters::Create(
    const grpc_channel_args* args,
    std::vector<const grpc_channel_filter*> filters) {
  // Attempt to create channel stack from requested filters.
  auto p = CreateChannelStack(args, std::move(filters));
  if (p.second != GRPC_ERROR_NONE) {
    // Channel stack creation failed with requested filters.
    // Create with lame filter instead.
    grpc_error_handle error = p.second;
    grpc_arg error_arg = MakeLameClientErrorArg(&error);
    grpc_channel_args* new_args =
        grpc_channel_args_copy_and_add(args, &error_arg, 1);
    GRPC_ERROR_UNREF(error);
    p = CreateChannelStack(new_args, {&grpc_lame_filter});
    GPR_ASSERT(p.second == GRPC_ERROR_NONE);
    grpc_channel_args_destroy(new_args);
  }
  return MakeRefCounted<DynamicFilters>(p.first);
}

DynamicFilters::~DynamicFilters() {
  GRPC_CHANNEL_STACK_UNREF(channel_stack_, "~DynamicFilters");
}

RefCountedPtr<DynamicFilters::Call> DynamicFilters::CreateCall(
    DynamicFilters::Call::Args args, grpc_error_handle* error) {
  size_t allocation_size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(Call)) +
                           channel_stack_->call_stack_size;
  Call* call = static_cast<Call*>(args.arena->Alloc(allocation_size));
  new (call) Call(std::move(args), error);
  return RefCountedPtr<DynamicFilters::Call>(call);
}

}  // namespace grpc_core
