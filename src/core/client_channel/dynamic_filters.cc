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

#include "src/core/client_channel/dynamic_filters.h"

#include <stddef.h>

#include <new>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/util/alloc.h"

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
      call_stack,         // call_stack
      nullptr,            // server_transport_data
      args.path,          // path
      args.start_time,    // start_time
      args.deadline,      // deadline
      args.arena,         // arena
      args.call_combiner  // call_combiner
  };
  *error = grpc_call_stack_init(channel_stack_->channel_stack_.get(), 1,
                                Destroy, this, &call_args);
  if (GPR_UNLIKELY(!error->ok())) {
    LOG(ERROR) << "error: " << StatusToString(*error);
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
  CHECK_EQ(after_call_stack_destroy_, nullptr);
  CHECK_NE(closure, nullptr);
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
  GRPC_CALL_STACK_UNREF(CALL_TO_CALL_STACK(this), "dynamic-filters-unref");
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

absl::StatusOr<RefCountedPtr<grpc_channel_stack>> CreateChannelStack(
    const ChannelArgs& args, std::vector<const grpc_channel_filter*> filters) {
  ChannelStackBuilderImpl builder("DynamicFilters", GRPC_CLIENT_DYNAMIC, args);
  for (auto filter : filters) {
    builder.AppendFilter(filter);
  }
  return builder.Build();
}

}  // namespace

RefCountedPtr<DynamicFilters> DynamicFilters::Create(
    const ChannelArgs& args, std::vector<const grpc_channel_filter*> filters) {
  // Attempt to create channel stack from requested filters.
  auto p = CreateChannelStack(args, std::move(filters));
  if (!p.ok()) {
    // Channel stack creation failed with requested filters.
    // Create with lame filter instead.
    auto error = p.status();
    p = CreateChannelStack(args.Set(MakeLameClientErrorArg(&error)),
                           {&LameClientFilter::kFilter});
  }
  return MakeRefCounted<DynamicFilters>(std::move(p.value()));
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
