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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_CLOSURE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_CLOSURE_H
#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/utility/utility.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace posix_engine {

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
    cb_(absl::exchange(status_, absl::OkStatus()));
    if (!is_permanent_) {
      delete this;
    }
  }

  // This closure clean doesn't itself up after execution. It is expected to be
  // cleaned up by the caller at the appropriate time.
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
  bool is_permanent_ = false;
  absl::Status status_;
};

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_CLOSURE_H
