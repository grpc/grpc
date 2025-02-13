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

#include <memory>
#include <string>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h"
#include "src/core/lib/event_engine/posix_engine/ev_poll_posix.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/no_destruct.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_POSIX_SOCKET_TCP
namespace {
// TODO(yijiem): this object is thread-unsafe, if we are creating pollers in
// multiple threads (e.g. multiple event engines) or if we are creating pollers
// while we are forking then we will run into issues.
grpc_core::NoDestruct<ObjectGroupForkHandler> g_poller_fork_manager;

class PollerForkCallbackMethods {
 public:
  static void Prefork() { g_poller_fork_manager->Prefork(); }
  static void PostforkParent() { g_poller_fork_manager->PostforkParent(); }
  static void PostforkChild() { g_poller_fork_manager->PostforkChild(); }
};

bool PollStrategyMatches(absl::string_view strategy, absl::string_view want) {
  return strategy == "all" || strategy == want;
}
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
  g_poller_fork_manager->RegisterForkable(
      poller, PollerForkCallbackMethods::Prefork,
      PollerForkCallbackMethods::PostforkParent,
      PollerForkCallbackMethods::PostforkChild);
  return poller;
}

#else  // GRPC_POSIX_SOCKET_TCP

std::shared_ptr<PosixEventPoller> MakeDefaultPoller(Scheduler* /*scheduler*/) {
  return nullptr;
}

#endif  // GRPC_POSIX_SOCKET_TCP

}  // namespace grpc_event_engine::experimental
