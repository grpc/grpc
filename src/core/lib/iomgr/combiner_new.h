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

#ifndef GRPC_CORE_LIB_IOMGR_COMBINER_NEW_H
#define GRPC_CORE_LIB_IOMGR_COMBINER_NEW_H

#include <grpc/support/port_platform.h>

#include <functional>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/mpscq.h"

// Provides serialized access to some resource.
// Each action queued on a combiner is executed serially in a borrowed thread.
// The actual thread executing actions may change over time (but there will only
// ever be one at a time).

namespace grpc_core {

extern DebugOnlyTraceFlag grpc_combiner_new_trace;

class Combiner {
 public:
  void Run(std::function<void()> callback,
           DebugLocation location, const char* reason);

 private:
  void DrainQueue();

  Atomic<size_t> size_ = 0;  // num closures in queue or currently executing
  MultiProducerSingleConsumerQueue queue_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_COMBINER_NEW_H */
