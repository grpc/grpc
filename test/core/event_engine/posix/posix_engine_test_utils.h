// Copyright 2022 gRPC Authors
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

#include <utility>

#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"

namespace grpc_event_engine {
namespace posix_engine {

class TestScheduler : public Scheduler {
 public:
  explicit TestScheduler(grpc_event_engine::experimental::EventEngine* engine)
      : engine_(engine) {}
  void Run(
      grpc_event_engine::experimental::EventEngine::Closure* closure) override {
    engine_->Run(closure);
  }

  void Run(absl::AnyInvocable<void()> cb) override {
    engine_->Run(std::move(cb));
  }

 private:
  grpc_event_engine::experimental::EventEngine* engine_;
};

// Creates a client socket and blocks until it connects to the specified
// server address. The function abort fails upon encountering errors.
int ConnectToServerOrDie(
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
        server_address);

}  // namespace posix_engine
}  // namespace grpc_event_engine