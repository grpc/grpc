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

#include "event_poller.h"

#include <grpc/support/alloc.h>

#include "src/core/lib/event_engine/iomgr_engine/ev_epoll1_linux.h"
#include "src/core/lib/event_engine/iomgr_engine/ev_poll_posix.h"
#include "src/core/lib/event_engine/iomgr_engine/event_poller.h"
#include "src/core/lib/gprpp/global_config.h"

GPR_GLOBAL_CONFIG_DECLARE_STRING(grpc_poll_strategy);

namespace grpc_event_engine {
namespace iomgr_engine {

namespace {

// Adapted from src/core/lib/iomgr/ev_posix.cc
void add(const char* beg, const char* end, char*** ss, size_t* ns) {
  size_t n = *ns;
  size_t np = n + 1;
  char* s;
  size_t len;
  len = static_cast<size_t>(end - beg);
  s = static_cast<char*>(gpr_malloc(len + 1));
  memcpy(s, beg, len);
  s[len] = 0;
  *ss = static_cast<char**>(gpr_realloc(*ss, sizeof(char**) * np));
  (*ss)[n] = s;
  *ns = np;
}

// Adapted from src/core/lib/iomgr/ev_posix.cc
void split(const char* s, char*** ss, size_t* ns) {
  const char* c = strchr(s, ',');
  if (c == nullptr) {
    add(s, s + strlen(s), ss, ns);
  } else {
    add(s, c, ss, ns);
    split(c + 1, ss, ns);
  }
}

// Adapted from src/core/lib/iomgr/ev_posix.cc
bool is(const char* want, const char* have) {
  return 0 == strcmp(want, "all") || 0 == strcmp(want, have);
}

}  // namespace

EventPoller* GetDefaultPoller(Scheduler* scheduler) {
  grpc_core::UniquePtr<char> poll_strategy =
      GPR_GLOBAL_CONFIG_GET(grpc_poll_strategy);
  EventPoller* poller = nullptr;
  char** strings = nullptr;
  size_t nstrings = 0;
  split(poll_strategy.get(), &strings, &nstrings);

  for (size_t i = 0; i < nstrings && poller == nullptr; i++) {
    if (is("epoll1", strings[i])) {
      poller = GetEpoll1Poller(scheduler);
    } else if (is("poll", strings[i])) {
      poller = GetPollPoller(scheduler, /*use_phony_poll=*/false);
    } else if (is("none", strings[i])) {
      poller = GetPollPoller(scheduler, /*use_phony_poll=*/true);
    }
  }

  for (size_t i = 0; i < nstrings; i++) {
    gpr_free(strings[i]);
  }
  gpr_free(strings);
  return poller;
}

}  // namespace iomgr_engine
}  // namespace grpc_event_engine
