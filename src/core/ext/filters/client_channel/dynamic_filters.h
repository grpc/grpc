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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_DYNAMIC_FILTERS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_DYNAMIC_FILTERS_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"

namespace grpc_core {

class DynamicFilters : public RefCounted<DynamicFilters> {
 public:
  // Implements the interface of RefCounted<>.
  class Call {
   public:
    struct Args {
      RefCountedPtr<DynamicFilters> channel_stack;
      grpc_polling_entity* pollent;
      grpc_slice path;
      gpr_cycle_counter start_time;
      grpc_millis deadline;
      Arena* arena;
      grpc_call_context_element* context;
      CallCombiner* call_combiner;
    };

    Call(Args args, grpc_error_handle* error);

    // Continues processing a transport stream op batch.
    void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

    // Sets the 'then_schedule_closure' argument for call stack destruction.
    // Must be called once per call.
    void SetAfterCallStackDestroy(grpc_closure* closure);

    // Interface of RefCounted<>.
    RefCountedPtr<Call> Ref() GRPC_MUST_USE_RESULT;
    RefCountedPtr<Call> Ref(const DebugLocation& location,
                            const char* reason) GRPC_MUST_USE_RESULT;
    // When refcount drops to 0, destroys itself and the associated call stack,
    // but does NOT free the memory because it's in the call arena.
    void Unref();
    void Unref(const DebugLocation& location, const char* reason);

   private:
    // Allow RefCountedPtr<> to access IncrementRefCount().
    template <typename T>
    friend class RefCountedPtr;

    // Interface of RefCounted<>.
    void IncrementRefCount();
    void IncrementRefCount(const DebugLocation& location, const char* reason);

    static void Destroy(void* arg, grpc_error_handle error);

    RefCountedPtr<DynamicFilters> channel_stack_;
    grpc_closure* after_call_stack_destroy_ = nullptr;
  };

  static RefCountedPtr<DynamicFilters> Create(
      const grpc_channel_args* args,
      std::vector<const grpc_channel_filter*> filters);

  explicit DynamicFilters(grpc_channel_stack* channel_stack)
      : channel_stack_(channel_stack) {}

  ~DynamicFilters() override;

  RefCountedPtr<Call> CreateCall(Call::Args args, grpc_error_handle* error);

 private:
  grpc_channel_stack* channel_stack_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_DYNAMIC_FILTERS_H
