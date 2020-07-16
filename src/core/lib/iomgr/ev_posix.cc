/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_EV

#include "src/core/lib/iomgr/ev_posix.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/iomgr/ev_epoll1_linux.h"
#include "src/core/lib/iomgr/ev_epollex_linux.h"
#include "src/core/lib/iomgr/ev_poll_posix.h"
#include "src/core/lib/iomgr/internal_errqueue.h"
#include "src/core/lib/iomgr/iomgr.h"

GPR_GLOBAL_CONFIG_DEFINE_STRING(
    grpc_poll_strategy, "all",
    "Declares which polling engines to try when starting gRPC. "
    "This is a comma-separated list of engines, which are tried in priority "
    "order first -> last.")

grpc_core::DebugOnlyTraceFlag grpc_polling_trace(
    false, "polling"); /* Disabled by default */

/* Traces fd create/close operations */
grpc_core::DebugOnlyTraceFlag grpc_fd_trace(false, "fd_trace");
grpc_core::DebugOnlyTraceFlag grpc_trace_fd_refcount(false, "fd_refcount");
grpc_core::DebugOnlyTraceFlag grpc_polling_api_trace(false, "polling_api");

// Polling API trace only enabled in debug builds
#ifndef NDEBUG
#define GRPC_POLLING_API_TRACE(format, ...)                  \
  if (GRPC_TRACE_FLAG_ENABLED(grpc_polling_api_trace)) {     \
    gpr_log(GPR_INFO, "(polling-api) " format, __VA_ARGS__); \
  }
#else
#define GRPC_POLLING_API_TRACE(...)
#endif  // NDEBUG

/** Default poll() function - a pointer so that it can be overridden by some
 *  tests */
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
static const char* g_poll_strategy_name = nullptr;

typedef const grpc_event_engine_vtable* (*event_engine_factory_fn)(
    bool explicit_request);

struct event_engine_factory {
  const char* name;
  event_engine_factory_fn factory;
};
namespace {

grpc_poll_function_type real_poll_function;

int dummy_poll(struct pollfd fds[], nfds_t nfds, int timeout) {
  if (timeout == 0) {
    return real_poll_function(fds, nfds, 0);
  } else {
    gpr_log(GPR_ERROR, "Attempted a blocking poll when declared non-polling.");
    GPR_ASSERT(false);
    return -1;
  }
}

const grpc_event_engine_vtable* init_non_polling(bool explicit_request) {
  if (!explicit_request) {
    return nullptr;
  }
  // return the simplest engine as a dummy but also override the poller
  auto ret = grpc_init_poll_posix(explicit_request);
  real_poll_function = grpc_poll_function;
  grpc_poll_function = dummy_poll;
  grpc_iomgr_mark_non_polling_internal();

  return ret;
}
}  // namespace

#define ENGINE_HEAD_CUSTOM "head_custom"
#define ENGINE_TAIL_CUSTOM "tail_custom"

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
static event_engine_factory g_factories[] = {
    {ENGINE_HEAD_CUSTOM, nullptr},        {ENGINE_HEAD_CUSTOM, nullptr},
    {ENGINE_HEAD_CUSTOM, nullptr},        {ENGINE_HEAD_CUSTOM, nullptr},
    {"epollex", grpc_init_epollex_linux}, {"epoll1", grpc_init_epoll1_linux},
    {"poll", grpc_init_poll_posix},       {"none", init_non_polling},
    {ENGINE_TAIL_CUSTOM, nullptr},        {ENGINE_TAIL_CUSTOM, nullptr},
    {ENGINE_TAIL_CUSTOM, nullptr},        {ENGINE_TAIL_CUSTOM, nullptr},
};

static void add(const char* beg, const char* end, char*** ss, size_t* ns) {
  size_t n = *ns;
  size_t np = n + 1;
  char* s;
  size_t len;
  GPR_ASSERT(end >= beg);
  len = static_cast<size_t>(end - beg);
  s = static_cast<char*>(gpr_malloc(len + 1));
  memcpy(s, beg, len);
  s[len] = 0;
  *ss = static_cast<char**>(gpr_realloc(*ss, sizeof(char**) * np));
  (*ss)[n] = s;
  *ns = np;
}

static void split(const char* s, char*** ss, size_t* ns) {
  const char* c = strchr(s, ',');
  if (c == nullptr) {
    add(s, s + strlen(s), ss, ns);
  } else {
    add(s, c, ss, ns);
    split(c + 1, ss, ns);
  }
}

static bool is(const char* want, const char* have) {
  return 0 == strcmp(want, "all") || 0 == strcmp(want, have);
}

static void try_engine(const char* engine) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_factories); i++) {
    if (g_factories[i].factory != nullptr && is(engine, g_factories[i].name)) {
      if ((g_event_engine = g_factories[i].factory(
               0 == strcmp(engine, g_factories[i].name)))) {
        g_poll_strategy_name = g_factories[i].name;
        gpr_log(GPR_DEBUG, "Using polling engine: %s", g_factories[i].name);
        return;
      }
    }
  }
}

/* Call this before calling grpc_event_engine_init() */
void grpc_register_event_engine_factory(const char* name,
                                        event_engine_factory_fn factory,
                                        bool add_at_head) {
  const char* custom_match =
      add_at_head ? ENGINE_HEAD_CUSTOM : ENGINE_TAIL_CUSTOM;

  // Overwrite an existing registration if already registered
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_factories); i++) {
    if (0 == strcmp(name, g_factories[i].name)) {
      g_factories[i].factory = factory;
      return;
    }
  }

  // Otherwise fill in an available custom slot
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_factories); i++) {
    if (0 == strcmp(g_factories[i].name, custom_match)) {
      g_factories[i].name = name;
      g_factories[i].factory = factory;
      return;
    }
  }

  // Otherwise fail
  GPR_ASSERT(false);
}

/*If grpc_event_engine_init() has been called, returns the poll_strategy_name.
 * Otherwise, returns nullptr. */
const char* grpc_get_poll_strategy_name() { return g_poll_strategy_name; }

void grpc_event_engine_init(void) {
  grpc_core::UniquePtr<char> value = GPR_GLOBAL_CONFIG_GET(grpc_poll_strategy);

  char** strings = nullptr;
  size_t nstrings = 0;
  split(value.get(), &strings, &nstrings);

  for (size_t i = 0; g_event_engine == nullptr && i < nstrings; i++) {
    try_engine(strings[i]);
  }

  for (size_t i = 0; i < nstrings; i++) {
    gpr_free(strings[i]);
  }
  gpr_free(strings);

  if (g_event_engine == nullptr) {
    gpr_log(GPR_ERROR, "No event engine could be initialized from %s",
            value.get());
    abort();
  }
}

void grpc_event_engine_shutdown(void) {
  g_event_engine->shutdown_engine();
  g_event_engine = nullptr;
}

bool grpc_event_engine_can_track_errors(void) {
  /* Only track errors if platform supports errqueue. */
  if (grpc_core::kernel_supports_errqueue()) {
    return g_event_engine->can_track_err;
  }
  return false;
}

bool grpc_event_engine_run_in_background(void) {
  // g_event_engine is nullptr when using a custom iomgr.
  return g_event_engine != nullptr && g_event_engine->run_in_background;
}

grpc_fd* grpc_fd_create(int fd, const char* name, bool track_err) {
  GRPC_POLLING_API_TRACE("fd_create(%d, %s, %d)", fd, name, track_err);
  GRPC_FD_TRACE("fd_create(%d, %s, %d)", fd, name, track_err);
  return g_event_engine->fd_create(
      fd, name, track_err && grpc_event_engine_can_track_errors());
}

int grpc_fd_wrapped_fd(grpc_fd* fd) {
  return g_event_engine->fd_wrapped_fd(fd);
}

void grpc_fd_orphan(grpc_fd* fd, grpc_closure* on_done, int* release_fd,
                    const char* reason) {
  GRPC_POLLING_API_TRACE("fd_orphan(%d, %p, %p, %s)", grpc_fd_wrapped_fd(fd),
                         on_done, release_fd, reason);
  GRPC_FD_TRACE("grpc_fd_orphan, fd:%d closed", grpc_fd_wrapped_fd(fd));

  g_event_engine->fd_orphan(fd, on_done, release_fd, reason);
}

void grpc_fd_shutdown(grpc_fd* fd, grpc_error* why) {
  GRPC_POLLING_API_TRACE("fd_shutdown(%d)", grpc_fd_wrapped_fd(fd));
  GRPC_FD_TRACE("fd_shutdown(%d)", grpc_fd_wrapped_fd(fd));
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
  GRPC_POLLING_API_TRACE("pollset_init(%p)", pollset);
  g_event_engine->pollset_init(pollset, mu);
}

static void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  GRPC_POLLING_API_TRACE("pollset_shutdown(%p)", pollset);
  g_event_engine->pollset_shutdown(pollset, closure);
}

static void pollset_destroy(grpc_pollset* pollset) {
  GRPC_POLLING_API_TRACE("pollset_destroy(%p)", pollset);
  g_event_engine->pollset_destroy(pollset);
}

static grpc_error* pollset_work(grpc_pollset* pollset,
                                grpc_pollset_worker** worker,
                                grpc_millis deadline) {
  GRPC_POLLING_API_TRACE("pollset_work(%p, %" PRId64 ") begin", pollset,
                         deadline);
  grpc_error* err = g_event_engine->pollset_work(pollset, worker, deadline);
  GRPC_POLLING_API_TRACE("pollset_work(%p, %" PRId64 ") end", pollset,
                         deadline);
  return err;
}

static grpc_error* pollset_kick(grpc_pollset* pollset,
                                grpc_pollset_worker* specific_worker) {
  GRPC_POLLING_API_TRACE("pollset_kick(%p, %p)", pollset, specific_worker);
  return g_event_engine->pollset_kick(pollset, specific_worker);
}

void grpc_pollset_add_fd(grpc_pollset* pollset, struct grpc_fd* fd) {
  GRPC_POLLING_API_TRACE("pollset_add_fd(%p, %d)", pollset,
                         grpc_fd_wrapped_fd(fd));
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
  GRPC_POLLING_API_TRACE("pollset_set_create(%p)", pss);
  return pss;
}

static void pollset_set_destroy(grpc_pollset_set* pollset_set) {
  GRPC_POLLING_API_TRACE("pollset_set_destroy(%p)", pollset_set);
  g_event_engine->pollset_set_destroy(pollset_set);
}

static void pollset_set_add_pollset(grpc_pollset_set* pollset_set,
                                    grpc_pollset* pollset) {
  GRPC_POLLING_API_TRACE("pollset_set_add_pollset(%p, %p)", pollset_set,
                         pollset);
  g_event_engine->pollset_set_add_pollset(pollset_set, pollset);
}

static void pollset_set_del_pollset(grpc_pollset_set* pollset_set,
                                    grpc_pollset* pollset) {
  GRPC_POLLING_API_TRACE("pollset_set_del_pollset(%p, %p)", pollset_set,
                         pollset);
  g_event_engine->pollset_set_del_pollset(pollset_set, pollset);
}

static void pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  GRPC_POLLING_API_TRACE("pollset_set_add_pollset_set(%p, %p)", bag, item);
  g_event_engine->pollset_set_add_pollset_set(bag, item);
}

static void pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  GRPC_POLLING_API_TRACE("pollset_set_del_pollset_set(%p, %p)", bag, item);
  g_event_engine->pollset_set_del_pollset_set(bag, item);
}

grpc_pollset_set_vtable grpc_posix_pollset_set_vtable = {
    pollset_set_create,          pollset_set_destroy,
    pollset_set_add_pollset,     pollset_set_del_pollset,
    pollset_set_add_pollset_set, pollset_set_del_pollset_set};

void grpc_pollset_set_add_fd(grpc_pollset_set* pollset_set, grpc_fd* fd) {
  GRPC_POLLING_API_TRACE("pollset_set_add_fd(%p, %d)", pollset_set,
                         grpc_fd_wrapped_fd(fd));
  g_event_engine->pollset_set_add_fd(pollset_set, fd);
}

void grpc_pollset_set_del_fd(grpc_pollset_set* pollset_set, grpc_fd* fd) {
  GRPC_POLLING_API_TRACE("pollset_set_del_fd(%p, %d)", pollset_set,
                         grpc_fd_wrapped_fd(fd));
  g_event_engine->pollset_set_del_fd(pollset_set, fd);
}

bool grpc_is_any_background_poller_thread(void) {
  return g_event_engine->is_any_background_poller_thread();
}

bool grpc_add_closure_to_background_poller(grpc_closure* closure,
                                           grpc_error* error) {
  return g_event_engine->add_closure_to_background_poller(closure, error);
}

void grpc_shutdown_background_closure(void) {
  g_event_engine->shutdown_background_closure();
}

#endif  // GRPC_POSIX_SOCKET_EV
