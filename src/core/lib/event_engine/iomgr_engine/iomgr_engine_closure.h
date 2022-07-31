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
#include "absl/synchronization/mutex.h"
#include "absl/types/variant.h"
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
  struct OK {};
  struct Delete {};
  using Result = absl::variant<OK, Delete>;
  IomgrEngineClosure() = default;
  IomgrEngineClosure(absl::AnyInvocable<Result(absl::Status)> cb,
                     bool is_permanent)
      : cb_(std::move(cb)),
        is_permanent_(is_permanent),
        status_(absl::OkStatus()) {}
  ~IomgrEngineClosure() final = default;
  void SetStatus(absl::Status status) { status_ = status; }
  void Run() override {
    // A mutex is required to ensure only one active thread is executing the
    // enclosed any-invocable callback. For e.g, an IomgrEngineClosure scheduled
    // for a NotifyOnRead operation may execute and immediately schedule another
    // NotifyOnRead operation within the callback body. The second NotifyOnRead
    // request may also be served immediately before the first callback exits.
    // This would lead to potential races in the callback body and complicate
    // cleanup behaviour. To prevent this, we use a mutex to ensure sequential
    // access to the callback. In previous iomgr implementations, ExecCtx played
    // this role and provided sequential access to the callback.
    //
    // This mutex should not lead to contention because it only ensures
    // sequential access between two consequtive read or two consequetive write
    // handling operations.
    absl::ReleasableMutexLock lock(&mu_);
    auto result = cb_(absl::exchange(status_, absl::OkStatus()));
    if (!is_permanent_ || absl::holds_alternative<Delete>(result)) {
      lock.Release();
      delete this;
    }
  }

  // This closure clean doesn't itself up after execution by default. To
  // gracefully cleanup the closure, the enclosed any-invocable callback must
  // return IomgrEngineclosure::Delete{}.
  static IomgrEngineClosure* ToPermanentClosure(
      absl::AnyInvocable<Result(absl::Status)> cb) {
    return new IomgrEngineClosure(std::move(cb), true);
  }

  // This closure clean's itself up after execution. It is expected to be
  // used only in tests.
  static IomgrEngineClosure* TestOnlyToClosure(
      absl::AnyInvocable<Result(absl::Status)> cb) {
    return new IomgrEngineClosure(std::move(cb), false);
  }

 private:
  absl::Mutex mu_;
  absl::AnyInvocable<Result(absl::Status)> cb_;
  bool is_permanent_ = false;
  absl::Status status_;
};

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_IOMGR_ENGINE_CLOSURE_H
