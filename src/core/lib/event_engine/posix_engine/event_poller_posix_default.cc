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

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h"
#include "src/core/lib/event_engine/posix_engine/ev_poll_posix.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP
GPR_GLOBAL_CONFIG_DECLARE_STRING(grpc_poll_strategy);
#endif

namespace grpc_event_engine {
namespace posix_engine {

#ifdef GRPC_POSIX_SOCKET_TCP
namespace {

bool PollStrategyMatches(absl::string_view strategy, absl::string_view want) {
  return strategy == "all" || strategy == want;
}

}  // namespace

PosixEventPoller* MakeDefaultPoller(Scheduler* scheduler) {
  static const char* poll_strategy =
      GPR_GLOBAL_CONFIG_GET(grpc_poll_strategy).release();
  PosixEventPoller* poller = nullptr;
  auto strings = absl::StrSplit(poll_strategy, ',');
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
  return poller;
}

#else  // GRPC_POSIX_SOCKET_TCP

PosixEventPoller* MakeDefaultPoller(Scheduler* /*scheduler*/) {
  return nullptr;
}

#endif  // GRPC_POSIX_SOCKET_TCP

}  // namespace posix_engine
}  // namespace grpc_event_engine
