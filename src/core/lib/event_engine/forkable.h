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

#include <memory>
#include <vector>

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"

namespace grpc_event_engine {
namespace experimental {

extern grpc_core::TraceFlag grpc_trace_fork;

#define GRPC_FORK_TRACE_LOG(format, ...)                 \
  do {                                                   \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_fork)) {      \
      gpr_log(GPR_DEBUG, "[fork] " format, __VA_ARGS__); \
    }                                                    \
  } while (0)

#define GRPC_FORK_TRACE_LOG_STRING(format) GRPC_FORK_TRACE_LOG("%s", format)

// An interface to be implemented by EventEngines that wish to have managed fork
// support.
class Forkable {
 public:
  virtual ~Forkable() = default;
  virtual void PrepareFork() = 0;
  virtual void PostforkParent() = 0;
  virtual void PostforkChild() = 0;
};

#ifndef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
// no-op ObjectGroupForkHandler impl
#else
class ObjectGroupForkHandler {
 public:
  // adds forkable to set of forkables
  // asserts if IsForking() == true
  void RegisterForkable(std::shared_ptr<Forkable> forkable);

  void Prefork();
  void PostforkParent();
  void PostforkChild();

 private:
  bool is_forking_ = false;
  std::vector<std::weak_ptr<Forkable> > forkables_;
};
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_FORKABLE_H
