// Copyright 2025 gRPC authors.
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

#ifndef GRPC_TEST_CORE_TEST_UTIL_TEST_TIMEOUT_H
#define GRPC_TEST_CORE_TEST_UTIL_TEST_TIMEOUT_H

#include <grpc/event_engine/event_engine.h>

#include "src/core/util/time.h"
#include "src/core/util/grpc_check.h"

namespace grpc_core {

// Instantiate this on the stack, giving a Duration timeout.
// When the timeout expires, this will crash via GRPC_ASSERT.
// If Cancel is called or this object drops out of scope, nothing
// will happen.
class TestTimeout {
 public:
  TestTimeout(
      Duration timeout,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine)
      : engine_(engine),
        timer_(engine->RunAfter(timeout,
                                []() { GRPC_CHECK(false); })) {}
  ~TestTimeout() { Cancel(); }

  void Cancel() { engine_->Cancel(timer_); }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine_;
  grpc_event_engine::experimental::EventEngine::TaskHandle timer_;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TEST_UTIL_TEST_TIMEOUT_H
