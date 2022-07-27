// Copyright 2022 The gRPC Authors
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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_IOMGR_ENGINE_CLOSURE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_IOMGR_ENGINE_CLOSURE_H
#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/utility/utility.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace iomgr_engine {

// The callbacks for Endpoint read and write take an absl::Status as
// argument - this is important for the tcp code to function correctly. We need
// a custom closure type because the default EventEngine::Closure type doesn't
// provide a way to pass a status when the callback is run.
class IomgrEngineClosure final
    : public grpc_event_engine::experimental::EventEngine::Closure {
 public:
  IomgrEngineClosure() = default;
  IomgrEngineClosure(absl::AnyInvocable<void(absl::Status)> cb,
                     bool is_permanent)
      : cb_(std::move(cb)),
        is_permanent_(is_permanent),
        status_(absl::OkStatus()) {}
  ~IomgrEngineClosure() final = default;
  void SetStatus(absl::Status status) { status_ = status; }
  void Run() override {
    if (is_permanent_) {
      // Take a ref to protect against premature deletion of this closure by
      // cb_;
      Ref();
    }
    cb_(absl::exchange(status_, absl::OkStatus()));
    // For the ref taken at the beginning of this function. If it is a temporary
    // closure, it will get deleted immediately.
    Unref();
  }

  // Ref/Unref methods should only be called on permanent closures.
  // Ref-counting methods are needed to allow external code to control the
  // life-time of a permanent closure.
  //
  // For safe operation, any external code which provides a permanent
  // IomgrEngineClosure to the NotifyOn*** or OrphanHandle methods of an
  // grpc_event_engine::iomgr_engine::EventHandle object should perform the
  // following steps:
  //  1. First take a Ref() on the closure
  //  2. Provide it to the desired NotifyOn*** or OrphanHandle method.
  //  3. Ensure that the any-invocable which was used to create the closure
  //     calls Unref() in its body.
  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  // This closure clean doesn't itself up after execution. It is expected to be
  // cleaned up by the caller at the appropriate time. The caller should call
  // Unref() at the time of cleanup.
  static IomgrEngineClosure* ToPermanentClosure(
      absl::AnyInvocable<void(absl::Status)> cb) {
    return new IomgrEngineClosure(std::move(cb), true);
  }

  // This closure clean's itself up after execution. It is expected to be
  // used only in tests.
  static IomgrEngineClosure* TestOnlyToClosure(
      absl::AnyInvocable<void(absl::Status)> cb) {
    return new IomgrEngineClosure(std::move(cb), false);
  }

 private:
  absl::AnyInvocable<void(absl::Status)> cb_;
  std::atomic<int> ref_count_{1};
  bool is_permanent_ = false;
  absl::Status status_;
};

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_IOMGR_ENGINE_CLOSURE_H
