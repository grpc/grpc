//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_EV

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/ev_epoll1_linux.h"
#include "src/core/lib/iomgr/ev_poll_posix.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/internal_errqueue.h"
#include "src/core/util/crash.h"
#include "src/core/util/useful.h"

/// Default poll() function - a pointer so that it can be overridden by some
/// tests
#ifndef GPR_AIX
grpc_poll_function_type grpc_poll_function = poll;
#else
int aix_poll(struct pollfd fds[], nfds_t nfds, int timeout) {
  return poll(fds, nfds, timeout);
}
grpc_poll_function_type grpc_poll_function = aix_poll;
#endif  // GPR_AIX

grpc_wakeup_fd grpc_global_wakeup_fd;

static const grpc_event_engine_vtable* g_event_engine = nullptr;
static gpr_once g_choose_engine = GPR_ONCE_INIT;

// The global array of event-engine factories. Each entry is a pair with a name
// and an event-engine generator function (nullptr if there is no generator
// registered for this name). The middle entries are the engines predefined by
// open-source gRPC. The head entries represent an opportunity for specific
// high-priority custom pollers to be added by the initializer plugins of
// custom-built gRPC libraries. The tail entries represent the same, but for
// low-priority custom pollers. The actual poller selected is either the first
// available one in the list if no specific poller is requested, or the first
// specific poller that is requested by name in the GRPC_POLL_STRATEGY
// environment variable if that variable is set (which should be a
// comma-separated list of one or more event engine names)
static const grpc_event_engine_vtable* g_vtables[] = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &grpc_ev_epoll1_posix,
    &grpc_ev_poll_posix,
    &grpc_ev_none_posix,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

static bool is(absl::string_view want, absl::string_view have) {
  return want == "all" || want == have;
}

static void try_engine(absl::string_view engine) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_vtables); i++) {
    if (g_vtables[i] != nullptr && is(engine, g_vtables[i]->name) &&
        g_vtables[i]->check_engine_available(engine == g_vtables[i]->name)) {
      g_event_engine = g_vtables[i];
      GRPC_TRACE_VLOG(polling_api, 2)
          << "Using polling engine: " << g_event_engine->name;
      return;
    }
  }
}

// Call this before calling grpc_event_engine_init()
void grpc_register_event_engine_factory(const grpc_event_engine_vtable* vtable,
                                        bool add_at_head) {
  const grpc_event_engine_vtable** first_null = nullptr;
  const grpc_event_engine_vtable** last_null = nullptr;

  // Overwrite an existing registration if already registered
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_vtables); i++) {
    if (g_vtables[i] == nullptr) {
      if (first_null == nullptr) first_null = &g_vtables[i];
      last_null = &g_vtables[i];
    } else if (0 == strcmp(g_vtables[i]->name, vtable->name)) {
      g_vtables[i] = vtable;
      return;
    }
  }

  *(add_at_head ? first_null : last_null) = vtable;
}

// If grpc_event_engine_init() has been called, returns the poll_strategy_name.
//  Otherwise, returns nullptr.
const char* grpc_get_poll_strategy_name() { return g_event_engine->name; }

void grpc_event_engine_init(void) {
  gpr_once_init(&g_choose_engine, []() {
    auto value = grpc_core::ConfigVars::Get().PollStrategy();
    for (auto trial : absl::StrSplit(value, ',')) {
      try_engine(trial);
      if (g_event_engine != nullptr) return;
    }

    if (g_event_engine == nullptr) {
      grpc_core::Crash(
          absl::StrFormat("No event engine could be initialized from %s",
                          std::string(value).c_str()));
    }
  });
  g_event_engine->init_engine();
}

void grpc_event_engine_shutdown(void) { g_event_engine->shutdown_engine(); }

bool grpc_event_engine_can_track_errors(void) {
  // Only track errors if platform supports errqueue.
  return grpc_core::KernelSupportsErrqueue() && g_event_engine->can_track_err;
}

bool grpc_event_engine_run_in_background(void) {
  // g_event_engine is nullptr when using a custom iomgr.
  return g_event_engine != nullptr && g_event_engine->run_in_background;
}

grpc_fd* grpc_fd_create(int fd, const char* name, bool track_err) {
  GRPC_TRACE_DLOG(polling_api, INFO) << "(polling-api) fd_create(" << fd << ", "
                                     << name << ", " << track_err << ")";
  GRPC_TRACE_LOG(fd_trace, INFO) << "(fd-trace) fd_create(" << fd << ", "
                                 << name << ", " << track_err << ")";
  return g_event_engine->fd_create(
      fd, name, track_err && grpc_event_engine_can_track_errors());
}

int grpc_fd_wrapped_fd(grpc_fd* fd) {
  return g_event_engine->fd_wrapped_fd(fd);
}

void grpc_fd_orphan(grpc_fd* fd, grpc_closure* on_done, int* release_fd,
                    const char* reason) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) fd_orphan(" << grpc_fd_wrapped_fd(fd) << ", " << on_done
      << ", " << release_fd << ", " << reason << ")";
  GRPC_TRACE_LOG(fd_trace, INFO)
      << "(fd-trace) grpc_fd_orphan, fd:" << grpc_fd_wrapped_fd(fd)
      << " closed";

  g_event_engine->fd_orphan(fd, on_done, release_fd, reason);
}

void grpc_fd_set_pre_allocated(grpc_fd* fd) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) fd_set_pre_allocated(" << grpc_fd_wrapped_fd(fd) << ")";
  GRPC_TRACE_LOG(fd_trace, INFO)
      << "(fd-trace) fd_set_pre_allocated(" << grpc_fd_wrapped_fd(fd) << ")";
  g_event_engine->fd_set_pre_allocated(fd);
}

void grpc_fd_shutdown(grpc_fd* fd, grpc_error_handle why) {
  GRPC_TRACE_LOG(polling_api, INFO)
      << "(polling-api) fd_shutdown(" << grpc_fd_wrapped_fd(fd) << ")";
  GRPC_TRACE_LOG(fd_trace, INFO)
      << "(fd-trace) fd_shutdown(" << grpc_fd_wrapped_fd(fd) << ")";
  g_event_engine->fd_shutdown(fd, why);
}

bool grpc_fd_is_shutdown(grpc_fd* fd) {
  return g_event_engine->fd_is_shutdown(fd);
}

void grpc_fd_notify_on_read(grpc_fd* fd, grpc_closure* closure) {
  g_event_engine->fd_notify_on_read(fd, closure);
}

void grpc_fd_notify_on_write(grpc_fd* fd, grpc_closure* closure) {
  g_event_engine->fd_notify_on_write(fd, closure);
}

void grpc_fd_notify_on_error(grpc_fd* fd, grpc_closure* closure) {
  g_event_engine->fd_notify_on_error(fd, closure);
}

void grpc_fd_set_readable(grpc_fd* fd) { g_event_engine->fd_set_readable(fd); }

void grpc_fd_set_writable(grpc_fd* fd) { g_event_engine->fd_set_writable(fd); }

void grpc_fd_set_error(grpc_fd* fd) { g_event_engine->fd_set_error(fd); }

static size_t pollset_size(void) { return g_event_engine->pollset_size; }

static void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_init(" << pollset << ")";
  g_event_engine->pollset_init(pollset, mu);
}

static void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_shutdown(" << pollset << ")";
  g_event_engine->pollset_shutdown(pollset, closure);
}

static void pollset_destroy(grpc_pollset* pollset) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_destroy(" << pollset << ")";
  g_event_engine->pollset_destroy(pollset);
}

static grpc_error_handle pollset_work(grpc_pollset* pollset,
                                      grpc_pollset_worker** worker,
                                      grpc_core::Timestamp deadline) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_work(" << pollset << ", "
      << deadline.milliseconds_after_process_epoch() << ") begin";
  grpc_error_handle err =
      g_event_engine->pollset_work(pollset, worker, deadline);
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_work(" << pollset << ", "
      << deadline.milliseconds_after_process_epoch() << ") end";
  return err;
}

static grpc_error_handle pollset_kick(grpc_pollset* pollset,
                                      grpc_pollset_worker* specific_worker) {
  GRPC_TRACE_DLOG(polling_api, INFO) << "(polling-api) pollset_kick(" << pollset
                                     << ", " << specific_worker << ")";
  return g_event_engine->pollset_kick(pollset, specific_worker);
}

void grpc_pollset_add_fd(grpc_pollset* pollset, struct grpc_fd* fd) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_add_fd(" << pollset << ", "
      << grpc_fd_wrapped_fd(fd) << ")";
  g_event_engine->pollset_add_fd(pollset, fd);
}

void pollset_global_init() {}
void pollset_global_shutdown() {}

grpc_pollset_vtable grpc_posix_pollset_vtable = {
    pollset_global_init, pollset_global_shutdown,
    pollset_init,        pollset_shutdown,
    pollset_destroy,     pollset_work,
    pollset_kick,        pollset_size};

static grpc_pollset_set* pollset_set_create(void) {
  grpc_pollset_set* pss = g_event_engine->pollset_set_create();
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_set_create(" << pss << ")";
  return pss;
}

static void pollset_set_destroy(grpc_pollset_set* pollset_set) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_set_destroy(" << pollset_set << ")";
  g_event_engine->pollset_set_destroy(pollset_set);
}

static void pollset_set_add_pollset(grpc_pollset_set* pollset_set,
                                    grpc_pollset* pollset) {
  GRPC_TRACE_DLOG(polling_api, INFO) << "(polling-api) pollset_set_add_pollset("
                                     << pollset_set << ", " << pollset << ")";
  g_event_engine->pollset_set_add_pollset(pollset_set, pollset);
}

static void pollset_set_del_pollset(grpc_pollset_set* pollset_set,
                                    grpc_pollset* pollset) {
  GRPC_TRACE_DLOG(polling_api, INFO) << "(polling-api) pollset_set_del_pollset("
                                     << pollset_set << ", " << pollset << ")";
  g_event_engine->pollset_set_del_pollset(pollset_set, pollset);
}

static void pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_set_add_pollset_set(" << bag << ", " << item
      << ")";
  g_event_engine->pollset_set_add_pollset_set(bag, item);
}

static void pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_set_del_pollset_set(" << bag << ", " << item
      << ")";
  g_event_engine->pollset_set_del_pollset_set(bag, item);
}

grpc_pollset_set_vtable grpc_posix_pollset_set_vtable = {
    pollset_set_create,          pollset_set_destroy,
    pollset_set_add_pollset,     pollset_set_del_pollset,
    pollset_set_add_pollset_set, pollset_set_del_pollset_set};

void grpc_pollset_set_add_fd(grpc_pollset_set* pollset_set, grpc_fd* fd) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_set_add_fd(" << pollset_set << ", "
      << grpc_fd_wrapped_fd(fd) << ")";
  g_event_engine->pollset_set_add_fd(pollset_set, fd);
}

void grpc_pollset_set_del_fd(grpc_pollset_set* pollset_set, grpc_fd* fd) {
  GRPC_TRACE_DLOG(polling_api, INFO)
      << "(polling-api) pollset_set_del_fd(" << pollset_set << ", "
      << grpc_fd_wrapped_fd(fd) << ")";
  g_event_engine->pollset_set_del_fd(pollset_set, fd);
}

bool grpc_is_any_background_poller_thread(void) {
  return g_event_engine->is_any_background_poller_thread();
}

bool grpc_add_closure_to_background_poller(grpc_closure* closure,
                                           grpc_error_handle error) {
  return g_event_engine->add_closure_to_background_poller(closure, error);
}

void grpc_shutdown_background_closure(void) {
  g_event_engine->shutdown_background_closure();
}

#else  // GRPC_POSIX_SOCKET_EV

const char* grpc_get_poll_strategy_name() { return ""; }

#endif  // GRPC_POSIX_SOCKET_EV
