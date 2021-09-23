/*
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <memory>

#include <grpc/support/log.h>
#include <grpcpp/alarm.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/time.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/completion_queue.h"

namespace grpc {

namespace internal {
class AlarmImpl : public ::grpc::internal::CompletionQueueTag {
 public:
  AlarmImpl() : cq_(nullptr), tag_(nullptr) {
    gpr_ref_init(&refs_, 1);
    grpc_timer_init_unset(&timer_);
  }
  ~AlarmImpl() override {}
  bool FinalizeResult(void** tag, bool* /*status*/) override {
    *tag = tag_;
    Unref();
    return true;
  }
  void Set(::grpc::CompletionQueue* cq, gpr_timespec deadline, void* tag) {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    GRPC_CQ_INTERNAL_REF(cq->cq(), "alarm");
    cq_ = cq->cq();
    tag_ = tag;
    GPR_ASSERT(grpc_cq_begin_op(cq_, this));
    GRPC_CLOSURE_INIT(
        &on_alarm_,
        [](void* arg, grpc_error_handle error) {
          // queue the op on the completion queue
          AlarmImpl* alarm = static_cast<AlarmImpl*>(arg);
          alarm->Ref();
          // Preserve the cq and reset the cq_ so that the alarm
          // can be reset when the alarm tag is delivered.
          grpc_completion_queue* cq = alarm->cq_;
          alarm->cq_ = nullptr;
          grpc_cq_end_op(
              cq, alarm, error,
              [](void* /*arg*/, grpc_cq_completion* /*completion*/) {}, arg,
              &alarm->completion_);
          GRPC_CQ_INTERNAL_UNREF(cq, "alarm");
        },
        this, grpc_schedule_on_exec_ctx);
    grpc_timer_init(&timer_, grpc_timespec_to_millis_round_up(deadline),
                    &on_alarm_);
  }
  void Set(gpr_timespec deadline, std::function<void(bool)> f) {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    // Don't use any CQ at all. Instead just use the timer to fire the function
    callback_ = std::move(f);
    Ref();
    GRPC_CLOSURE_INIT(
        &on_alarm_,
        [](void* arg, grpc_error_handle error) {
          grpc_core::Executor::Run(
              GRPC_CLOSURE_CREATE(
                  [](void* arg, grpc_error_handle error) {
                    AlarmImpl* alarm = static_cast<AlarmImpl*>(arg);
                    alarm->callback_(error == GRPC_ERROR_NONE);
                    alarm->Unref();
                  },
                  arg, nullptr),
              error);
        },
        this, grpc_schedule_on_exec_ctx);
    grpc_timer_init(&timer_, grpc_timespec_to_millis_round_up(deadline),
                    &on_alarm_);
  }
  void Cancel() {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    grpc_timer_cancel(&timer_);
  }
  void Destroy() {
    Cancel();
    Unref();
  }

 private:
  void Ref() { gpr_ref(&refs_); }
  void Unref() {
    if (gpr_unref(&refs_)) {
      delete this;
    }
  }

  grpc_timer timer_;
  gpr_refcount refs_;
  grpc_closure on_alarm_;
  grpc_cq_completion completion_;
  // completion queue where events about this alarm will be posted
  grpc_completion_queue* cq_;
  void* tag_;
  std::function<void(bool)> callback_;
};
}  // namespace internal

static ::grpc::internal::GrpcLibraryInitializer g_gli_initializer;

Alarm::Alarm() : alarm_(new internal::AlarmImpl()) {
  g_gli_initializer.summon();
}

void Alarm::SetInternal(::grpc::CompletionQueue* cq, gpr_timespec deadline,
                        void* tag) {
  // Note that we know that alarm_ is actually an internal::AlarmImpl
  // but we declared it as the base pointer to avoid a forward declaration
  // or exposing core data structures in the C++ public headers.
  // Thus it is safe to use a static_cast to the subclass here, and the
  // C++ style guide allows us to do so in this case
  static_cast<internal::AlarmImpl*>(alarm_)->Set(cq, deadline, tag);
}

void Alarm::SetInternal(gpr_timespec deadline, std::function<void(bool)> f) {
  // Note that we know that alarm_ is actually an internal::AlarmImpl
  // but we declared it as the base pointer to avoid a forward declaration
  // or exposing core data structures in the C++ public headers.
  // Thus it is safe to use a static_cast to the subclass here, and the
  // C++ style guide allows us to do so in this case
  static_cast<internal::AlarmImpl*>(alarm_)->Set(deadline, std::move(f));
}

Alarm::~Alarm() {
  if (alarm_ != nullptr) {
    static_cast<internal::AlarmImpl*>(alarm_)->Destroy();
  }
}

void Alarm::Cancel() { static_cast<internal::AlarmImpl*>(alarm_)->Cancel(); }
}  // namespace grpc
