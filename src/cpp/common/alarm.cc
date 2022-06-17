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

#include <functional>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpcpp/alarm.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/codegen/completion_queue_tag.h>
#include <grpcpp/impl/grpc_library.h>

#include "src/core/lib/event_engine/event_engine_factory.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/completion_queue.h"

namespace grpc {

namespace internal {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

class AlarmImpl : public grpc::internal::CompletionQueueTag {
 public:
  AlarmImpl() : cq_(nullptr), tag_(nullptr) { gpr_ref_init(&refs_, 1); }
  ~AlarmImpl() override {}
  bool FinalizeResult(void** tag, bool* /*status*/) override {
    *tag = tag_;
    Unref();
    return true;
  }
  void Set(grpc::CompletionQueue* cq, gpr_timespec deadline, void* tag) {
    grpc_core::MutexLock lock(&mu_);
    GPR_ASSERT(!cq_timer_handle_.has_value() &&
               !callback_timer_handle_.has_value());
    grpc_core::ExecCtx exec_ctx;
    GRPC_CQ_INTERNAL_REF(cq->cq(), "alarm");
    cq_ = cq->cq();
    tag_ = tag;
    GPR_ASSERT(grpc_cq_begin_op(cq_, this));
    Ref();
    cq_timer_handle_ = GetDefaultEventEngine()->RunAfter(
        grpc_core::Timestamp::FromTimespecRoundUp(deadline) -
            grpc_core::ExecCtx::Get()->Now(),
        [this] { OnCQAlarm(GRPC_ERROR_NONE); });
  }
  void Set(gpr_timespec deadline, std::function<void(bool)> f) {
    grpc_core::MutexLock lock(&mu_);
    GPR_ASSERT(!cq_timer_handle_.has_value() &&
               !callback_timer_handle_.has_value());
    grpc_core::ExecCtx exec_ctx;
    // Don't use any CQ at all. Instead just use the timer to fire the function
    callback_ = std::move(f);
    Ref();
    callback_timer_handle_ = GetDefaultEventEngine()->RunAfter(
        grpc_core::Timestamp::FromTimespecRoundUp(deadline) -
            grpc_core::ExecCtx::Get()->Now(),
        [this] { OnCallbackAlarm(true); });
  }
  void Cancel() {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::MutexLock lock(&mu_);
    if (callback_timer_handle_.has_value() &&
        GetDefaultEventEngine()->Cancel(*callback_timer_handle_)) {
      GetDefaultEventEngine()->Run(
          [this] { OnCallbackAlarm(/*is_ok=*/false); });
      callback_timer_handle_.reset();
    } else if (cq_timer_handle_.has_value() &&
               GetDefaultEventEngine()->Cancel(*cq_timer_handle_)) {
      GetDefaultEventEngine()->Run([this] { OnCQAlarm(GRPC_ERROR_CANCELLED); });
      cq_timer_handle_.reset();
    }
  }
  void Destroy() {
    Cancel();
    Unref();
  }

 private:
  void OnCQAlarm(grpc_error_handle error) {
    {
      grpc_core::MutexLock lock(&mu_);
      cq_timer_handle_.reset();
    }
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    // Preserve the cq and reset the cq_ so that the alarm
    // can be reset when the alarm tag is delivered.
    grpc_completion_queue* cq = cq_;
    cq_ = nullptr;
    grpc_cq_end_op(
        cq, this, error,
        [](void* /*arg*/, grpc_cq_completion* /*completion*/) {}, nullptr,
        &completion_);
    GRPC_CQ_INTERNAL_UNREF(cq, "alarm");
  }

  void OnCallbackAlarm(bool is_ok) {
    {
      grpc_core::MutexLock lock(&mu_);
      callback_timer_handle_.reset();
    }
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    callback_(is_ok);
    Unref();
  }

  void Ref() { gpr_ref(&refs_); }
  void Unref() {
    if (gpr_unref(&refs_)) {
      delete this;
    }
  }

  grpc_core::Mutex mu_;
  absl::optional<EventEngine::TaskHandle> cq_timer_handle_ ABSL_GUARDED_BY(mu_);
  absl::optional<EventEngine::TaskHandle> callback_timer_handle_
      ABSL_GUARDED_BY(mu_);
  gpr_refcount refs_;
  grpc_cq_completion completion_;
  // completion queue where events about this alarm will be posted
  grpc_completion_queue* cq_;
  void* tag_;
  std::function<void(bool)> callback_;
};
}  // namespace internal

static grpc::internal::GrpcLibraryInitializer g_gli_initializer;

Alarm::Alarm() : alarm_(new internal::AlarmImpl()) {
  g_gli_initializer.summon();
}

void Alarm::SetInternal(grpc::CompletionQueue* cq, gpr_timespec deadline,
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
