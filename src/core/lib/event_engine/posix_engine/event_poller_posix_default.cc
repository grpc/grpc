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

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h"
#include "src/core/lib/event_engine/posix_engine/ev_poll_posix.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_SOCKET_TCP
namespace {

grpc_core::NoDestruct<std::vector<std::weak_ptr<ForkableInterface>>>
    g_forkable_instances;

bool g_registered{false};

bool PollStrategyMatches(absl::string_view strategy, absl::string_view want) {
  return strategy == "all" || strategy == want;
}

#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

void grpc_prefork() {
  gpr_log(GPR_INFO, "grpc_prefork()");
  for (auto& instance : *g_forkable_instances) {
    auto shared = instance.lock();
    if (shared) {
      shared->PrepareFork();
    }
  }
}

void grpc_postfork_parent() {
  gpr_log(GPR_INFO, "grpc_postfork_parent()");
  for (auto& instance : *g_forkable_instances) {
    auto shared = instance.lock();
    if (shared) {
      shared->PostforkParent();
    }
  }
}

void grpc_postfork_child() {
  gpr_log(GPR_INFO, "grpc_postfork_child()");
  for (auto& instance : *g_forkable_instances) {
    auto shared = instance.lock();
    if (shared) {
      shared->PostforkChild();
    }
  }
}

#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

}  // namespace

std::shared_ptr<PosixEventPoller> MakeDefaultPoller(Scheduler* scheduler) {
  std::shared_ptr<PosixEventPoller> poller;
  auto strings =
      absl::StrSplit(grpc_core::ConfigVars::Get().PollStrategy(), ',');
  for (auto it = strings.begin(); it != strings.end() && poller == nullptr;
       it++) {
    if (PollStrategyMatches(*it, "epoll1")) {
      poller = MakeEpoll1Poller(scheduler);
    }
    if (poller == nullptr && PollStrategyMatches(*it, "poll")) {
      // If epoll1 fails and if poll strategy matches "poll", use Poll poller
      poller = MakePollPoller(scheduler, /*use_phony_poll=*/false);
    } else if (poller == nullptr && PollStrategyMatches(*it, "none")) {
      // If epoll1 fails and if poll strategy matches "none", use phony poll
      // poller.
      poller = MakePollPoller(scheduler, /*use_phony_poll=*/true);
    }
  }
  if (!std::exchange(g_registered, true)) {
#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
    pthread_atfork(grpc_prefork, grpc_postfork_parent, grpc_postfork_child);
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  }
  g_forkable_instances->emplace_back(poller);
  return poller;
}

#else  // GRPC_POSIX_SOCKET_TCP

std::shared_ptr<PosixEventPoller> MakeDefaultPoller(Scheduler* /*scheduler*/) {
  return nullptr;
}

#endif  // GRPC_POSIX_SOCKET_TCP

}  // namespace experimental
}  // namespace grpc_event_engine
