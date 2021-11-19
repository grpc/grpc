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

#include "absl/functional/bind_front.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>
#include <grpcpp/alarm.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/time.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/event_engine_factory.h"
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
    GRPC_CQ_INTERNAL_REF(cq->cq(), "alarm");
    cq_ = cq->cq();
    tag_ = tag;
    GPR_ASSERT(grpc_cq_begin_op(cq_, this));
    closure_ = [this](grpc_error_handle error) {
      Ref();
      // Preserve the cq and reset the cq_ so that the alarm
      // can be reset when the alarm tag is delivered.
      grpc_completion_queue* cq = cq_;
      cq_ = nullptr;
      grpc_cq_end_op(
          cq, this, error,
          [](void* /*arg*/, grpc_cq_completion* /*completion*/) {}, this,
          &completion_);
      GRPC_CQ_INTERNAL_UNREF(cq, "alarm");
    };
    // TODO(hork): options for EE ownership:
    // * global EE as shown below,
    // * an alarm-owned EE (terrible idea included for completeness)
    // * adding a new Alarm constructor that takes a shared EE, maybe
    //   indirectly through ChannelArgs
    timer_handle_ = GetDefaultEventEngine()->RunAt(
        grpc_core::ToAbslTime(deadline),
        absl::bind_front(closure_, GRPC_ERROR_NONE));
  }
  void Set(gpr_timespec deadline, std::function<void(bool)> f) {
    // Don't use any CQ at all. Instead just use the timer to fire the function
    Ref();
    closure_ = [this, f](grpc_error_handle error) {
      f(error == GRPC_ERROR_NONE);
      Unref();
    };
    // TODO(hork): see above for EE ownership ideas
    timer_handle_ = GetDefaultEventEngine()->RunAt(
        grpc_core::ToAbslTime(deadline),
        absl::bind_front(closure_, GRPC_ERROR_NONE));
  }

  void Cancel() {
    if (GetDefaultEventEngine()->Cancel(timer_handle_)) {
      Ref();
      GetDefaultEventEngine()->Run([this]() {
        closure_(GRPC_ERROR_CANCELLED);
        Unref();
      });
    }
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

  EventEngine::TaskHandle timer_handle_ = {{-1, -1}};
  std::function<void(grpc_error_handle)> closure_;
  gpr_refcount refs_;
  grpc_cq_completion completion_;
  // completion queue where events about this alarm will be posted
  grpc_completion_queue* cq_;
  void* tag_;
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
