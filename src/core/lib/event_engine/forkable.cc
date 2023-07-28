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

#include "absl/base/thread_annotations.h"

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

grpc_core::TraceFlag grpc_trace_fork(false, "fork");
grpc_core::TraceFlag grpc_trace_fork_stacktrace(false, "fork_stacktrace");

namespace {
grpc_core::NoDestruct<grpc_core::Mutex> g_mu;
bool g_registered ABSL_GUARDED_BY(g_mu){false};

// This must be ordered because there are ordering dependencies between
// certain fork handlers.
grpc_core::NoDestruct<std::vector<Forkable*>> g_forkables ABSL_GUARDED_BY(g_mu);

bool IsForkEnabled() {
  static bool enabled = grpc_core::ConfigVars::Get().EnableForkSupport();
  return enabled;
}
}  // namespace

Forkable::Forkable() { ManageForkable(this); }

Forkable::~Forkable() { StopManagingForkable(this); }

void RegisterForkHandlers() {
  if (IsForkEnabled()) {
    grpc_core::MutexLock lock(g_mu.get());
    if (!std::exchange(g_registered, true)) {
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
      GRPC_FORK_TRACE_LOG_STRING("RegisterForkHandlers");
      pthread_atfork(PrepareFork, PostforkParent, PostforkChild);
#endif
    }
  }
}

void PrepareFork() {
  if (IsForkEnabled()) {
    GRPC_FORK_TRACE_LOG_STRING("PrepareFork");
    grpc_core::MutexLock lock(g_mu.get());
    for (auto forkable_iter = g_forkables->rbegin();
         forkable_iter != g_forkables->rend(); ++forkable_iter) {
      (*forkable_iter)->PrepareFork();
    }
    GRPC_FORK_TRACE_LOG_STRING("PrepareFork finished");
  }
}

void PostforkParent() {
  if (IsForkEnabled()) {
    GRPC_FORK_TRACE_LOG_STRING("PostforkParent");
    grpc_core::MutexLock lock(g_mu.get());
    for (auto* forkable : *g_forkables) {
      GRPC_FORK_TRACE_LOG("Calling PostforkParent for forkable::%p", forkable);
      forkable->PostforkParent();
    }
    GRPC_FORK_TRACE_LOG_STRING("PostforkParent finished");
  }
}

void PostforkChild() {
  if (IsForkEnabled()) {
    GRPC_FORK_TRACE_LOG_STRING("PostforkChild");
    grpc_core::MutexLock lock(g_mu.get());
    for (auto* forkable : *g_forkables) {
      GRPC_FORK_TRACE_LOG("Calling PostforkChild for forkable::%p", forkable);
      forkable->PostforkChild();
    }
    GRPC_FORK_TRACE_LOG_STRING("PostforkChild finished");
  }
}

void ManageForkable(Forkable* forkable) {
  if (IsForkEnabled()) {
    GRPC_FORK_TRACE_LOG("Manage forkable::%p", forkable);
    grpc_core::MutexLock lock(g_mu.get());
    g_forkables->push_back(forkable);
  }
}

void StopManagingForkable(Forkable* forkable) {
  if (IsForkEnabled()) {
    GRPC_FORK_TRACE_LOG("Stop managing forkable::%p", forkable);
    grpc_core::MutexLock lock(g_mu.get());
    auto iter = std::find(g_forkables->begin(), g_forkables->end(), forkable);
    GPR_ASSERT(iter != g_forkables->end());
    g_forkables->erase(iter);
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
