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
#include <utility>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h"
#include "src/core/lib/event_engine/posix_engine/ev_poll_posix.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_POSIX_SOCKET_TCP
namespace {

bool PollStrategyMatches(absl::string_view strategy, absl::string_view want) {
  return strategy == "all" || strategy == want;
}
}  // namespace

std::shared_ptr<PosixEventPoller> MakeDefaultPoller(
    std::shared_ptr<ThreadPool> thread_pool) {
  // Note that Make*Poller are allowed to return nullptr - e.g. MakeEpoll1Poller
  // would return nullptr if an epoll poller is requested on unsupported
  // platform.
  std::shared_ptr<PosixEventPoller> poller;
  auto strings =
      absl::StrSplit(grpc_core::ConfigVars::Get().PollStrategy(), ',');
  for (auto it = strings.begin(); it != strings.end() && poller == nullptr;
       it++) {
    if (PollStrategyMatches(*it, "epoll1")) {
      poller = MakeEpoll1Poller(thread_pool);
    }
    if (poller == nullptr && PollStrategyMatches(*it, "poll")) {
      // If epoll1 fails and if poll strategy matches "poll", use Poll poller
      poller = MakePollPoller(thread_pool, /*use_phony_poll=*/false);
    } else if (poller == nullptr && PollStrategyMatches(*it, "none")) {
      // If epoll1 fails and if poll strategy matches "none", use phony poll
      // poller.
      poller = MakePollPoller(thread_pool, /*use_phony_poll=*/true);
    }
  }
  return poller;
}

#else  // GRPC_POSIX_SOCKET_TCP

std::shared_ptr<PosixEventPoller> MakeDefaultPoller(
    std::shared_ptr<ThreadPool> /*thread_pool*/) {
  return nullptr;
}

#endif  // GRPC_POSIX_SOCKET_TCP

}  // namespace grpc_event_engine::experimental
