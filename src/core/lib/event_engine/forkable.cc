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

#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

#include <pthread.h>

#include "absl/container/flat_hash_set.h"
// #include "absl/container/btree_set.h"

#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/sync.h"

#include <iostream>

namespace grpc_event_engine {
namespace experimental {

namespace {
grpc_core::NoDestruct<grpc_core::Mutex> g_mu;
bool g_registered ABSL_GUARDED_BY(g_mu){false};

// This must be ordered because there are ordering dependencies between
// certain fork handlers.
grpc_core::NoDestruct<std::vector<Forkable*>> g_forkables
    ABSL_GUARDED_BY(g_mu);
}  // namespace

Forkable::Forkable() { ManageForkable(this); }

Forkable::~Forkable() { StopManagingForkable(this); }

void RegisterForkHandlers() {
  grpc_core::MutexLock lock(g_mu.get());
  if (!std::exchange(g_registered, true)) {
    pthread_atfork(PrepareFork, PostforkParent, PostforkChild);
  }
};

void PrepareFork() {
  std::cerr << "AAAAAAAAAAAAAAAAAAa Entering PrepareFork" << std::endl << std::flush;
  grpc_core::MutexLock lock(g_mu.get());
  // for (auto* forkable : *g_forkables) {
  for (auto forkable_iter = g_forkables->rbegin(); forkable_iter != g_forkables->rend(); ++forkable_iter) {
    std::cerr << "AAAAAAAAAAAAAAAAAAa Calling forkable->PrepareFork " << *forkable_iter << std::endl << std::flush;
    (*forkable_iter)->PrepareFork();
    std::cerr << "AAAAAAAAAAAAAAAAAAa Called forkable->PrepareFork " << *forkable_iter << std::endl << std::flush;
  }
  std::cerr << "AAAAAAAAAAAAAAAAAAa Exiting PrepareFork" << std::endl << std::flush;
}
void PostforkParent() {
  std::cerr << "AAAAAAAAAAAAAAAAAAa Entering PostforkParent" << std::endl << std::flush;
  grpc_core::MutexLock lock(g_mu.get());
  for (auto* forkable : *g_forkables) {
    forkable->PostforkParent();
  }
  std::cerr << "AAAAAAAAAAAAAAAAAAa Exiting PostforkParent" << std::endl << std::flush;
}

void PostforkChild() {
  std::cerr << "AAAAAAAAAAAAAAAAAAa Entering PostforkChild" << std::endl << std::flush;
  grpc_core::MutexLock lock(g_mu.get());
  for (auto* forkable : *g_forkables) {
    forkable->PostforkChild();
  }
  std::cerr << "AAAAAAAAAAAAAAAAAAa Exiting PostforkChild" << std::endl << std::flush;
}

void ManageForkable(Forkable* forkable) {
  grpc_core::MutexLock lock(g_mu.get());
  g_forkables->push_back(forkable);
}

void StopManagingForkable(Forkable* forkable) {
  grpc_core::MutexLock lock(g_mu.get());
  auto iter = std::find(g_forkables->begin(), g_forkables->end(), forkable);
  GPR_ASSERT(iter != g_forkables->end());
  g_forkables->erase(iter);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#else  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

#error This isn't working

namespace grpc_event_engine {
namespace experimental {

Forkable::Forkable() {}
Forkable::~Forkable() {}

void RegisterForkHandlers() {}
void PrepareFork() {}
void PostforkParent() {}
void PostforkChild() {}

void ManageForkable(Forkable* /* forkable */) {}
void StopManagingForkable(Forkable* /* forkable */) {}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
