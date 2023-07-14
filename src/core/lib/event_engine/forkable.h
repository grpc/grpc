// Copyright 2022 gRPC authors.
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_FORKABLE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_FORKABLE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace.h"

extern grpc_core::TraceFlag grpc_trace_fork;

#define GRPC_FORK_TRACE_LOG(format, ...)                 \
  do {                                                   \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_fork)) {      \
      gpr_log(GPR_DEBUG, "[fork] " format, __VA_ARGS__); \
    }                                                    \
  } while (0)

#define GRPC_FORK_TRACE_LOG_STRING(format) GRPC_FORK_TRACE_LOG("%s", format)

namespace grpc_event_engine {
namespace experimental {

// Register fork handlers with the system, enabling fork support.
//
// This provides pthread-based support for fork events. Any objects that
// implement Forkable can register themselves with this system using
// ManageForkable, and their respective methods will be called upon fork.
//
// This should be called once upon grpc_initialization.
void RegisterForkHandlers();

// Global callback for pthread_atfork's *prepare argument
void PrepareFork();
// Global callback for pthread_atfork's *parent argument
void PostforkParent();
// Global callback for pthread_atfork's *child argument
void PostforkChild();

// An interface to be implemented by EventEngines that wish to have managed fork
// support.
class Forkable {
 public:
  Forkable();
  virtual ~Forkable();
  virtual void PrepareFork() = 0;
  virtual void PostforkParent() = 0;
  virtual void PostforkChild() = 0;
};

// Add Forkables from the set of objects that are supported.
// Upon fork, each forkable will have its respective fork hooks called on
// the thread that invoked the fork.
//
// Relative ordering of fork callback operations is not guaranteed.
void ManageForkable(Forkable* forkable);
// Remove a forkable from the managed set.
void StopManagingForkable(Forkable* forkable);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_FORKABLE_H
