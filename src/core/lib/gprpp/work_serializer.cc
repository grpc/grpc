//
// Copyright 2019 gRPC authors.
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

#include "src/core/lib/gprpp/work_serializer.h"

#include <algorithm>
#include <memory>
#include <thread>
#include <utility>

#include "absl/container/inlined_vector.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_work_serializer_trace(false, "work_serializer");

//
// WorkSerializer::WorkSerializerImpl
//

class WorkSerializer::WorkSerializerImpl
    : public RefCounted<WorkSerializerImpl> {
 public:
  explicit WorkSerializerImpl(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine)
      : event_engine_(std::move(event_engine)) {}
  void Run(absl::AnyInvocable<void()> callback, DebugLocation location);

#ifndef NDEBUG
  bool RunningInWorkSerializer() const {
    return std::this_thread::get_id() == current_thread_;
  }
#endif

 private:
  struct CallbackWrapper {
    CallbackWrapper(absl::AnyInvocable<void()> cb, DebugLocation loc)
        : callback(std::move(cb)), location(loc) {}

    absl::AnyInvocable<void()> callback;
    DebugLocation location;
  };
  using CallbackVector = absl::InlinedVector<CallbackWrapper, 1>;

  bool Refill() ABSL_EXCLUSIVE_LOCKS_REQUIRED(processing_mu_, incoming_mu_) {
    incoming_callbacks_.swap(processing_callbacks_);
    std::reverse(processing_callbacks_.begin(), processing_callbacks_.end());
    return !processing_callbacks_.empty();
  }

  void FirstStep() ABSL_LOCKS_EXCLUDED(incoming_mu_, processing_mu_);
  void Step() ABSL_EXCLUSIVE_LOCKS_REQUIRED(processing_mu_)
      ABSL_LOCKS_EXCLUDED(incoming_mu_);

  Mutex incoming_mu_;
  Mutex processing_mu_ ABSL_ACQUIRED_BEFORE(incoming_mu_);
  bool running_ ABSL_GUARDED_BY(incoming_mu_) = false;
  // Queue of incoming callbacks
  CallbackVector incoming_callbacks_ ABSL_GUARDED_BY(incoming_mu_);
  // Queue of in-process callbacks, in reverse order
  // When this empties we take all of incoming_callbacks_ and reverse it
  // so we can just pop_back() to process the queue.
  CallbackVector processing_callbacks_ ABSL_GUARDED_BY(processing_mu_);
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;

#ifndef NDEBUG
  std::thread::id current_thread_;
#endif
};

void WorkSerializer::WorkSerializerImpl::FirstStep() {
  MutexLock lock(&processing_mu_);
  {
    MutexLock incoming_lock(&incoming_mu_);
    GPR_ASSERT(Refill());
  }
  Step();
}

void WorkSerializer::WorkSerializerImpl::Step() {
  // It's safe to have processing_mu_ held here because there's no path through
  // which processing_mu_ could be called from a callback.
#ifndef NDEBUG
  current_thread_ = std::this_thread::get_id();
#endif
  processing_callbacks_.back().callback();
  processing_callbacks_.pop_back();
#ifndef NDEBUG
  current_thread_ = std::thread::id();
#endif
  if (processing_callbacks_.empty()) {
    MutexLock incoming_lock(&incoming_mu_);
    if (!Refill()) {
      running_ = false;
      return;
    }
  }
  event_engine_->Run([self = Ref()]() {
    ApplicationCallbackExecCtx app_exec_ctx;
    ExecCtx exec_ctx;
    MutexLock lock(&self->processing_mu_);
    self->Step();
  });
}

void WorkSerializer::WorkSerializerImpl::Run(
    absl::AnyInvocable<void()> callback, DebugLocation location) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Run() %p Scheduling callback [%s:%d]",
            this, location.file(), location.line());
  }
  MutexLock incoming_lock(&incoming_mu_);
  incoming_callbacks_.emplace_back(std::move(callback), location);
  if (!std::exchange(running_, true)) {
    event_engine_->Run([self = Ref()]() {
      ApplicationCallbackExecCtx app_exec_ctx;
      ExecCtx exec_ctx;
      self->FirstStep();
    });
  }
}

//
// WorkSerializer
//

WorkSerializer::WorkSerializer(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : impl_(MakeRefCounted<WorkSerializerImpl>(std::move(event_engine))) {}

WorkSerializer::~WorkSerializer() {}

void WorkSerializer::Run(absl::AnyInvocable<void()> callback,
                         DebugLocation location) {
  impl_->Run(std::move(callback), location);
}

#ifndef NDEBUG
bool WorkSerializer::RunningInWorkSerializer() const {
  return impl_->RunningInWorkSerializer();
}
#endif

}  // namespace grpc_core
