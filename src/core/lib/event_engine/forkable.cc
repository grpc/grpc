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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/forkable.h"

#include <grpc/support/log.h>

#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
#include <pthread.h>
#endif

#include <algorithm>
#include <utility>
#include <vector>

#include "src/core/lib/config/config_vars.h"

namespace grpc_event_engine {
namespace experimental {

grpc_core::TraceFlag grpc_trace_fork(false, "fork");

namespace {
bool IsForkEnabled() {
  static bool enabled = grpc_core::ConfigVars::Get().EnableForkSupport();
  return enabled;
}
}  // namespace

void ObjectGroupForkHandler::RegisterForkable(
    std::shared_ptr<Forkable> forkable, GRPC_UNUSED void (*prepare)(void),
    GRPC_UNUSED void (*parent)(void), GRPC_UNUSED void (*child)(void)) {
  if (IsForkEnabled()) {
    GPR_ASSERT(!is_forking_);
    forkables_.emplace_back(forkable);
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
    if (!std::exchange(registered_, true)) {
      pthread_atfork(prepare, parent, child);
    }
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  }
}

void ObjectGroupForkHandler::Prefork() {
  if (IsForkEnabled()) {
    GPR_ASSERT(!std::exchange(is_forking_, true));
    GRPC_FORK_TRACE_LOG_STRING("PrepareFork");
    for (auto it = forkables_.begin(); it != forkables_.end();) {
      auto shared = it->lock();
      if (shared) {
        shared->PrepareFork();
        ++it;
      } else {
        it = forkables_.erase(it);
      }
    }
  }
}

void ObjectGroupForkHandler::PostforkParent() {
  if (IsForkEnabled()) {
    GPR_ASSERT(is_forking_);
    GRPC_FORK_TRACE_LOG_STRING("PostforkParent");
    for (auto it = forkables_.begin(); it != forkables_.end();) {
      auto shared = it->lock();
      if (shared) {
        shared->PostforkParent();
        ++it;
      } else {
        it = forkables_.erase(it);
      }
    }
    is_forking_ = false;
  }
}

void ObjectGroupForkHandler::PostforkChild() {
  if (IsForkEnabled()) {
    GPR_ASSERT(is_forking_);
    GRPC_FORK_TRACE_LOG_STRING("PostforkChild");
    for (auto it = forkables_.begin(); it != forkables_.end();) {
      auto shared = it->lock();
      if (shared) {
        shared->PostforkChild();
        ++it;
      } else {
        it = forkables_.erase(it);
      }
    }
    is_forking_ = false;
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
