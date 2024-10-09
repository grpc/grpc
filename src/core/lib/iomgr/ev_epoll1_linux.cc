//
//
// Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"

// This polling engine is only relevant on linux kernels supporting epoll
// epoll_create() or epoll_create1()
#ifdef GRPC_LINUX_EPOLL
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/ev_epoll1_linux.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/lockfree_event.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/manual_constructor.h"
#include "src/core/util/strerror.h"
#include "src/core/util/string.h"
#include "src/core/util/useful.h"

static grpc_wakeup_fd global_wakeup_fd;
static bool g_is_shutdown = true;

//******************************************************************************
// Singleton epoll set related fields
//

#define MAX_EPOLL_EVENTS 100
#define MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION 1

// NOTE ON SYNCHRONIZATION:
// - Fields in this struct are only modified by the designated poller. Hence
//   there is no need for any locks to protect the struct.
// - num_events and cursor fields have to be of atomic type to provide memory
//   visibility guarantees only. i.e In case of multiple pollers, the designated
//   polling thread keeps changing; the thread that wrote these values may be
//   different from the thread reading the values
//
typedef struct epoll_set {
  int epfd;

  // The epoll_events after the last call to epoll_wait()
  struct epoll_event events[MAX_EPOLL_EVENTS];

  // The number of epoll_events after the last call to epoll_wait()
  gpr_atm num_events;

  // Index of the first event in epoll_events that has to be processed. This
  // field is only valid if num_events > 0
  gpr_atm cursor;
} epoll_set;

// The global singleton epoll set
static epoll_set g_epoll_set;

static int epoll_create_and_cloexec() {
#ifdef GRPC_LINUX_EPOLL_CREATE1
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    LOG(ERROR) << "epoll_create1 unavailable";
  }
#else
  int fd = epoll_create(MAX_EPOLL_EVENTS);
  if (fd < 0) {
    LOG(ERROR) << "epoll_create unavailable";
  } else if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    LOG(ERROR) << "fcntl following epoll_create failed";
    return -1;
  }
#endif
  return fd;
}

// Must be called *only* once
static bool epoll_set_init() {
  g_epoll_set.epfd = epoll_create_and_cloexec();
  if (g_epoll_set.epfd < 0) {
    return false;
  }

  GRPC_TRACE_LOG(polling, INFO) << "grpc epoll fd: " << g_epoll_set.epfd;
  gpr_atm_no_barrier_store(&g_epoll_set.num_events, 0);
  gpr_atm_no_barrier_store(&g_epoll_set.cursor, 0);
  return true;
}

// epoll_set_init() MUST be called before calling this.
static void epoll_set_shutdown() {
  if (g_epoll_set.epfd >= 0) {
    close(g_epoll_set.epfd);
    g_epoll_set.epfd = -1;
  }
}

//******************************************************************************
// Fd Declarations
//

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
struct grpc_fork_fd_list {
  grpc_fd* fd;
  grpc_fd* next;
  grpc_fd* prev;
};

struct grpc_fd {
  int fd;

  grpc_core::ManualConstructor<grpc_core::LockfreeEvent> read_closure;
  grpc_core::ManualConstructor<grpc_core::LockfreeEvent> write_closure;
  grpc_core::ManualConstructor<grpc_core::LockfreeEvent> error_closure;

  struct grpc_fd* freelist_next;

  grpc_iomgr_object iomgr_object;

  // Only used when GRPC_ENABLE_FORK_SUPPORT=1
  grpc_fork_fd_list* fork_fd_list;

  bool is_pre_allocated;
};

static void fd_global_init(void);
static void fd_global_shutdown(void);

//******************************************************************************
// Pollset Declarations
//

typedef enum { UNKICKED, KICKED, DESIGNATED_POLLER } kick_state;

static const char* kick_state_string(kick_state st) {
  switch (st) {
    case UNKICKED:
      return "UNKICKED";
    case KICKED:
      return "KICKED";
    case DESIGNATED_POLLER:
      return "DESIGNATED_POLLER";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

struct grpc_pollset_worker {
  kick_state state;
  int kick_state_mutator;  // which line of code last changed kick state
  bool initialized_cv;
  grpc_pollset_worker* next;
  grpc_pollset_worker* prev;
  gpr_cv cv;
  grpc_closure_list schedule_on_end_work;
};

#define SET_KICK_STATE(worker, kick_state)   \
  do {                                       \
    (worker)->state = (kick_state);          \
    (worker)->kick_state_mutator = __LINE__; \
  } while (false)

#define MAX_NEIGHBORHOODS 1024u

typedef struct pollset_neighborhood {
  union {
    char pad[GPR_CACHELINE_SIZE];
    struct {
      gpr_mu mu;
      grpc_pollset* active_root;
    };
  };
} pollset_neighborhood;

struct grpc_pollset {
  gpr_mu mu;
  pollset_neighborhood* neighborhood;
  bool reassigning_neighborhood;
  grpc_pollset_worker* root_worker;
  bool kicked_without_poller;

  // Set to true if the pollset is observed to have no workers available to
  // poll
  bool seen_inactive;
  bool shutting_down;              // Is the pollset shutting down ?
  grpc_closure* shutdown_closure;  // Called after shutdown is complete

  // Number of workers who are *about-to* attach themselves to the pollset
  // worker list
  int begin_refs;

  grpc_pollset* next;
  grpc_pollset* prev;
};

//******************************************************************************
// Pollset-set Declarations
//

struct grpc_pollset_set {
  char unused;
};

//******************************************************************************
// Common helpers
//

static bool append_error(grpc_error_handle* composite, grpc_error_handle error,
                         const char* desc) {
  if (error.ok()) return true;
  if (composite->ok()) {
    *composite = GRPC_ERROR_CREATE(desc);
  }
  *composite = grpc_error_add_child(*composite, error);
  return false;
}

//******************************************************************************
// Fd Definitions
//

// We need to keep a freelist not because of any concerns of malloc performance
// but instead so that implementations with multiple threads in (for example)
// epoll_wait deal with the race between pollset removal and incoming poll
// notifications.
//
// The problem is that the poller ultimately holds a reference to this
// object, so it is very difficult to know when is safe to free it, at least
// without some expensive synchronization.
//
// If we keep the object freelisted, in the worst case losing this race just
// becomes a spurious read notification on a reused fd.
//

// The alarm system needs to be able to wakeup 'some poller' sometimes
// (specifically when a new alarm needs to be triggered earlier than the next
// alarm 'epoch'). This wakeup_fd gives us something to alert on when such a
// case occurs.

static grpc_fd* fd_freelist = nullptr;
static gpr_mu fd_freelist_mu;

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
static grpc_fd* fork_fd_list_head = nullptr;
static gpr_mu fork_fd_list_mu;

static void fd_global_init(void) { gpr_mu_init(&fd_freelist_mu); }

static void fd_global_shutdown(void) {
  // TODO(guantaol): We don't have a reasonable explanation about this
  // lock()/unlock() pattern. It can be a valid barrier if there is at most one
  // pending lock() at this point. Otherwise, there is still a possibility of
  // use-after-free race. Need to reason about the code and/or clean it up.
  gpr_mu_lock(&fd_freelist_mu);
  gpr_mu_unlock(&fd_freelist_mu);
  while (fd_freelist != nullptr) {
    grpc_fd* fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
    gpr_free(fd);
  }
  gpr_mu_destroy(&fd_freelist_mu);
}

static void fork_fd_list_add_grpc_fd(grpc_fd* fd) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    fd->fork_fd_list =
        static_cast<grpc_fork_fd_list*>(gpr_malloc(sizeof(grpc_fork_fd_list)));
    fd->fork_fd_list->next = fork_fd_list_head;
    fd->fork_fd_list->prev = nullptr;
    if (fork_fd_list_head != nullptr) {
      fork_fd_list_head->fork_fd_list->prev = fd;
    }
    fork_fd_list_head = fd;
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

static void fork_fd_list_remove_grpc_fd(grpc_fd* fd) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    if (fork_fd_list_head == fd) {
      fork_fd_list_head = fd->fork_fd_list->next;
    }
    if (fd->fork_fd_list->prev != nullptr) {
      fd->fork_fd_list->prev->fork_fd_list->next = fd->fork_fd_list->next;
    }
    if (fd->fork_fd_list->next != nullptr) {
      fd->fork_fd_list->next->fork_fd_list->prev = fd->fork_fd_list->prev;
    }
    gpr_free(fd->fork_fd_list);
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

static grpc_fd* fd_create(int fd, const char* name, bool track_err) {
  grpc_fd* new_fd = nullptr;

  gpr_mu_lock(&fd_freelist_mu);
  if (fd_freelist != nullptr) {
    new_fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
  }
  gpr_mu_unlock(&fd_freelist_mu);

  if (new_fd == nullptr) {
    new_fd = static_cast<grpc_fd*>(gpr_malloc(sizeof(grpc_fd)));
    new_fd->read_closure.Init();
    new_fd->write_closure.Init();
    new_fd->error_closure.Init();
  }
  new_fd->fd = fd;
  new_fd->read_closure->InitEvent();
  new_fd->write_closure->InitEvent();
  new_fd->error_closure->InitEvent();

  new_fd->freelist_next = nullptr;
  new_fd->is_pre_allocated = false;

  std::string fd_name = absl::StrCat(name, " fd=", fd);
  grpc_iomgr_register_object(&new_fd->iomgr_object, fd_name.c_str());
  fork_fd_list_add_grpc_fd(new_fd);
#ifndef NDEBUG
  GRPC_TRACE_VLOG(fd_refcount, 2)
      << "FD " << fd << " " << new_fd << " create " << fd_name;
#endif

  struct epoll_event ev;
  ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLOUT | EPOLLET);
  // Use the least significant bit of ev.data.ptr to store track_err. We expect
  // the addresses to be word aligned. We need to store track_err to avoid
  // synchronization issues when accessing it after receiving an event.
  // Accessing fd would be a data race there because the fd might have been
  // returned to the free list at that point.
  ev.data.ptr = reinterpret_cast<void*>(reinterpret_cast<intptr_t>(new_fd) |
                                        (track_err ? 1 : 0));
  if (epoll_ctl(g_epoll_set.epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    LOG(ERROR) << "epoll_ctl failed: " << grpc_core::StrError(errno);
  }

  return new_fd;
}

static int fd_wrapped_fd(grpc_fd* fd) { return fd->fd; }

// if 'releasing_fd' is true, it means that we are going to detach the internal
// fd from grpc_fd structure (i.e which means we should not be calling
// shutdown() syscall on that fd)
static void fd_shutdown_internal(grpc_fd* fd, grpc_error_handle why,
                                 bool releasing_fd) {
  if (fd->read_closure->SetShutdown(why)) {
    if (!releasing_fd) {
      if (!fd->is_pre_allocated) {
        shutdown(fd->fd, SHUT_RDWR);
      }
    } else {
      // we need a phony event for earlier linux versions.
      epoll_event phony_event;
      if (epoll_ctl(g_epoll_set.epfd, EPOLL_CTL_DEL, fd->fd, &phony_event) !=
          0) {
        LOG(ERROR) << "epoll_ctl failed: " << grpc_core::StrError(errno);
      }
    }
    fd->write_closure->SetShutdown(why);
    fd->error_closure->SetShutdown(why);
  }
}

// Might be called multiple times
static void fd_shutdown(grpc_fd* fd, grpc_error_handle why) {
  fd_shutdown_internal(fd, why, false);
}

static void fd_orphan(grpc_fd* fd, grpc_closure* on_done, int* release_fd,
                      const char* reason) {
  grpc_error_handle error;
  bool is_release_fd = (release_fd != nullptr);

  if (!fd->read_closure->IsShutdown()) {
    fd_shutdown_internal(fd, GRPC_ERROR_CREATE(reason), is_release_fd);
  }

  // If release_fd is not NULL, we should be relinquishing control of the file
  // descriptor fd->fd (but we still own the grpc_fd structure).
  if (is_release_fd) {
    *release_fd = fd->fd;
  } else {
    if (!fd->is_pre_allocated) {
      close(fd->fd);
    }
  }

  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, error);

  grpc_iomgr_unregister_object(&fd->iomgr_object);
  fork_fd_list_remove_grpc_fd(fd);
  fd->read_closure->DestroyEvent();
  fd->write_closure->DestroyEvent();
  fd->error_closure->DestroyEvent();

  gpr_mu_lock(&fd_freelist_mu);
  fd->freelist_next = fd_freelist;
  fd_freelist = fd;
  gpr_mu_unlock(&fd_freelist_mu);
}

static bool fd_is_shutdown(grpc_fd* fd) {
  return fd->read_closure->IsShutdown();
}

static void fd_notify_on_read(grpc_fd* fd, grpc_closure* closure) {
  fd->read_closure->NotifyOn(closure);
}

static void fd_notify_on_write(grpc_fd* fd, grpc_closure* closure) {
  fd->write_closure->NotifyOn(closure);
}

static void fd_notify_on_error(grpc_fd* fd, grpc_closure* closure) {
  fd->error_closure->NotifyOn(closure);
}

static void fd_become_readable(grpc_fd* fd) { fd->read_closure->SetReady(); }

static void fd_become_writable(grpc_fd* fd) { fd->write_closure->SetReady(); }

static void fd_has_errors(grpc_fd* fd) { fd->error_closure->SetReady(); }

static void fd_set_pre_allocated(grpc_fd* fd) { fd->is_pre_allocated = true; }

//******************************************************************************
// Pollset Definitions
//

static thread_local grpc_pollset* g_current_thread_pollset;
static thread_local grpc_pollset_worker* g_current_thread_worker;

// The designated poller
static gpr_atm g_active_poller;

static pollset_neighborhood* g_neighborhoods;
static size_t g_num_neighborhoods;

// Return true if first in list
static bool worker_insert(grpc_pollset* pollset, grpc_pollset_worker* worker) {
  if (pollset->root_worker == nullptr) {
    pollset->root_worker = worker;
    worker->next = worker->prev = worker;
    return true;
  } else {
    worker->next = pollset->root_worker;
    worker->prev = worker->next->prev;
    worker->next->prev = worker;
    worker->prev->next = worker;
    return false;
  }
}

// Return true if last in list
typedef enum { EMPTIED, NEW_ROOT, REMOVED } worker_remove_result;

static worker_remove_result worker_remove(grpc_pollset* pollset,
                                          grpc_pollset_worker* worker) {
  if (worker == pollset->root_worker) {
    if (worker == worker->next) {
      pollset->root_worker = nullptr;
      return EMPTIED;
    } else {
      pollset->root_worker = worker->next;
      worker->prev->next = worker->next;
      worker->next->prev = worker->prev;
      return NEW_ROOT;
    }
  } else {
    worker->prev->next = worker->next;
    worker->next->prev = worker->prev;
    return REMOVED;
  }
}

static size_t choose_neighborhood(void) {
  return static_cast<size_t>(gpr_cpu_current_cpu()) % g_num_neighborhoods;
}

static grpc_error_handle pollset_global_init(void) {
  gpr_atm_no_barrier_store(&g_active_poller, 0);
  global_wakeup_fd.read_fd = -1;
  grpc_error_handle err = grpc_wakeup_fd_init(&global_wakeup_fd);
  if (!err.ok()) return err;
  struct epoll_event ev;
  ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLET);
  ev.data.ptr = &global_wakeup_fd;
  if (epoll_ctl(g_epoll_set.epfd, EPOLL_CTL_ADD, global_wakeup_fd.read_fd,
                &ev) != 0) {
    return GRPC_OS_ERROR(errno, "epoll_ctl");
  }
  g_num_neighborhoods =
      grpc_core::Clamp(gpr_cpu_num_cores(), 1u, MAX_NEIGHBORHOODS);
  g_neighborhoods = static_cast<pollset_neighborhood*>(
      gpr_zalloc(sizeof(*g_neighborhoods) * g_num_neighborhoods));
  for (size_t i = 0; i < g_num_neighborhoods; i++) {
    gpr_mu_init(&g_neighborhoods[i].mu);
  }
  return absl::OkStatus();
}

static void pollset_global_shutdown(void) {
  if (global_wakeup_fd.read_fd != -1) grpc_wakeup_fd_destroy(&global_wakeup_fd);
  for (size_t i = 0; i < g_num_neighborhoods; i++) {
    gpr_mu_destroy(&g_neighborhoods[i].mu);
  }
  gpr_free(g_neighborhoods);
}

static void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
  pollset->neighborhood = &g_neighborhoods[choose_neighborhood()];
  pollset->reassigning_neighborhood = false;
  pollset->root_worker = nullptr;
  pollset->kicked_without_poller = false;
  pollset->seen_inactive = true;
  pollset->shutting_down = false;
  pollset->shutdown_closure = nullptr;
  pollset->begin_refs = 0;
  pollset->next = pollset->prev = nullptr;
}

static void pollset_destroy(grpc_pollset* pollset) {
  gpr_mu_lock(&pollset->mu);
  if (!pollset->seen_inactive) {
    pollset_neighborhood* neighborhood = pollset->neighborhood;
    gpr_mu_unlock(&pollset->mu);
  retry_lock_neighborhood:
    gpr_mu_lock(&neighborhood->mu);
    gpr_mu_lock(&pollset->mu);
    if (!pollset->seen_inactive) {
      if (pollset->neighborhood != neighborhood) {
        gpr_mu_unlock(&neighborhood->mu);
        neighborhood = pollset->neighborhood;
        gpr_mu_unlock(&pollset->mu);
        goto retry_lock_neighborhood;
      }
      pollset->prev->next = pollset->next;
      pollset->next->prev = pollset->prev;
      if (pollset == pollset->neighborhood->active_root) {
        pollset->neighborhood->active_root =
            pollset->next == pollset ? nullptr : pollset->next;
      }
    }
    gpr_mu_unlock(&pollset->neighborhood->mu);
  }
  gpr_mu_unlock(&pollset->mu);
  gpr_mu_destroy(&pollset->mu);
}

static grpc_error_handle pollset_kick_all(grpc_pollset* pollset) {
  grpc_error_handle error;
  if (pollset->root_worker != nullptr) {
    grpc_pollset_worker* worker = pollset->root_worker;
    do {
      switch (worker->state) {
        case KICKED:
          break;
        case UNKICKED:
          SET_KICK_STATE(worker, KICKED);
          if (worker->initialized_cv) {
            gpr_cv_signal(&worker->cv);
          }
          break;
        case DESIGNATED_POLLER:
          SET_KICK_STATE(worker, KICKED);
          append_error(&error, grpc_wakeup_fd_wakeup(&global_wakeup_fd),
                       "pollset_kick_all");
          break;
      }

      worker = worker->next;
    } while (worker != pollset->root_worker);
  }
  // TODO(sreek): Check if we need to set 'kicked_without_poller' to true here
  // in the else case
  return error;
}

static void pollset_maybe_finish_shutdown(grpc_pollset* pollset) {
  if (pollset->shutdown_closure != nullptr && pollset->root_worker == nullptr &&
      pollset->begin_refs == 0) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, pollset->shutdown_closure,
                            absl::OkStatus());
    pollset->shutdown_closure = nullptr;
  }
}

static void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  CHECK_EQ(pollset->shutdown_closure, nullptr);
  CHECK(!pollset->shutting_down);
  pollset->shutdown_closure = closure;
  pollset->shutting_down = true;
  GRPC_LOG_IF_ERROR("pollset_shutdown", pollset_kick_all(pollset));
  pollset_maybe_finish_shutdown(pollset);
}

static int poll_deadline_to_millis_timeout(grpc_core::Timestamp millis) {
  if (millis == grpc_core::Timestamp::InfFuture()) return -1;
  int64_t delta = (millis - grpc_core::Timestamp::Now()).millis();
  if (delta > INT_MAX) {
    return INT_MAX;
  } else if (delta < 0) {
    return 0;
  } else {
    return static_cast<int>(delta);
  }
}

// Process the epoll events found by do_epoll_wait() function.
// - g_epoll_set.cursor points to the index of the first event to be processed
// - This function then processes up-to MAX_EPOLL_EVENTS_PER_ITERATION and
//   updates the g_epoll_set.cursor

// NOTE ON SYNCRHONIZATION: Similar to do_epoll_wait(), this function is only
// called by g_active_poller thread. So there is no need for synchronization
// when accessing fields in g_epoll_set
static grpc_error_handle process_epoll_events(grpc_pollset* /*pollset*/) {
  static const char* err_desc = "process_events";
  grpc_error_handle error;
  long num_events = gpr_atm_acq_load(&g_epoll_set.num_events);
  long cursor = gpr_atm_acq_load(&g_epoll_set.cursor);
  for (int idx = 0;
       (idx < MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION) && cursor != num_events;
       idx++) {
    long c = cursor++;
    struct epoll_event* ev = &g_epoll_set.events[c];
    void* data_ptr = ev->data.ptr;

    if (data_ptr == &global_wakeup_fd) {
      append_error(&error, grpc_wakeup_fd_consume_wakeup(&global_wakeup_fd),
                   err_desc);
    } else {
      grpc_fd* fd = reinterpret_cast<grpc_fd*>(
          reinterpret_cast<intptr_t>(data_ptr) & ~intptr_t{1});
      bool track_err = reinterpret_cast<intptr_t>(data_ptr) & intptr_t{1};
      bool cancel = (ev->events & EPOLLHUP) != 0;
      bool error = (ev->events & EPOLLERR) != 0;
      bool read_ev = (ev->events & (EPOLLIN | EPOLLPRI)) != 0;
      bool write_ev = (ev->events & EPOLLOUT) != 0;
      bool err_fallback = error && !track_err;

      if (error && !err_fallback) {
        fd_has_errors(fd);
      }

      if (read_ev || cancel || err_fallback) {
        fd_become_readable(fd);
      }

      if (write_ev || cancel || err_fallback) {
        fd_become_writable(fd);
      }
    }
  }
  gpr_atm_rel_store(&g_epoll_set.cursor, cursor);
  return error;
}

// Do epoll_wait and store the events in g_epoll_set.events field. This does not
// "process" any of the events yet; that is done in process_epoll_events().
// *See process_epoll_events() function for more details.

// NOTE ON SYNCHRONIZATION: At any point of time, only the g_active_poller
// (i.e the designated poller thread) will be calling this function. So there is
// no need for any synchronization when accesing fields in g_epoll_set
static grpc_error_handle do_epoll_wait(grpc_pollset* ps,
                                       grpc_core::Timestamp deadline) {
  int r;
  int timeout = poll_deadline_to_millis_timeout(deadline);
  if (timeout != 0) {
    GRPC_SCHEDULING_START_BLOCKING_REGION;
  }
  do {
    r = epoll_wait(g_epoll_set.epfd, g_epoll_set.events, MAX_EPOLL_EVENTS,
                   timeout);
  } while (r < 0 && errno == EINTR);
  if (timeout != 0) {
    GRPC_SCHEDULING_END_BLOCKING_REGION;
  }

  if (r < 0) return GRPC_OS_ERROR(errno, "epoll_wait");

  GRPC_TRACE_LOG(polling, INFO)
      << "ps: " << ps << " poll got " << r << " events";

  gpr_atm_rel_store(&g_epoll_set.num_events, r);
  gpr_atm_rel_store(&g_epoll_set.cursor, 0);

  return absl::OkStatus();
}

static bool begin_worker(grpc_pollset* pollset, grpc_pollset_worker* worker,
                         grpc_pollset_worker** worker_hdl,
                         grpc_core::Timestamp deadline) {
  if (worker_hdl != nullptr) *worker_hdl = worker;
  worker->initialized_cv = false;
  SET_KICK_STATE(worker, UNKICKED);
  worker->schedule_on_end_work = (grpc_closure_list)GRPC_CLOSURE_LIST_INIT;
  pollset->begin_refs++;

  GRPC_TRACE_LOG(polling, INFO)
      << "PS:" << pollset << " BEGIN_STARTS:" << worker;

  if (pollset->seen_inactive) {
    // pollset has been observed to be inactive, we need to move back to the
    // active list
    bool is_reassigning = false;
    if (!pollset->reassigning_neighborhood) {
      is_reassigning = true;
      pollset->reassigning_neighborhood = true;
      pollset->neighborhood = &g_neighborhoods[choose_neighborhood()];
    }
    pollset_neighborhood* neighborhood = pollset->neighborhood;
    gpr_mu_unlock(&pollset->mu);
  // pollset unlocked: state may change (even worker->kick_state)
  retry_lock_neighborhood:
    gpr_mu_lock(&neighborhood->mu);
    gpr_mu_lock(&pollset->mu);
    GRPC_TRACE_LOG(polling, INFO)
        << "PS:" << pollset << " BEGIN_REORG:" << worker
        << " kick_state=" << kick_state_string(worker->state)
        << " is_reassigning=" << is_reassigning;
    if (pollset->seen_inactive) {
      if (neighborhood != pollset->neighborhood) {
        gpr_mu_unlock(&neighborhood->mu);
        neighborhood = pollset->neighborhood;
        gpr_mu_unlock(&pollset->mu);
        goto retry_lock_neighborhood;
      }

      // In the brief time we released the pollset locks above, the worker MAY
      // have been kicked. In this case, the worker should get out of this
      // pollset ASAP and hence this should neither add the pollset to
      // neighborhood nor mark the pollset as active.

      // On a side note, the only way a worker's kick state could have changed
      // at this point is if it were "kicked specifically". Since the worker has
      // not added itself to the pollset yet (by calling worker_insert()), it is
      // not visible in the "kick any" path yet
      if (worker->state == UNKICKED) {
        pollset->seen_inactive = false;
        if (neighborhood->active_root == nullptr) {
          neighborhood->active_root = pollset->next = pollset->prev = pollset;
          // Make this the designated poller if there isn't one already
          if (worker->state == UNKICKED &&
              gpr_atm_no_barrier_cas(&g_active_poller, 0,
                                     reinterpret_cast<gpr_atm>(worker))) {
            SET_KICK_STATE(worker, DESIGNATED_POLLER);
          }
        } else {
          pollset->next = neighborhood->active_root;
          pollset->prev = pollset->next->prev;
          pollset->next->prev = pollset->prev->next = pollset;
        }
      }
    }
    if (is_reassigning) {
      CHECK(pollset->reassigning_neighborhood);
      pollset->reassigning_neighborhood = false;
    }
    gpr_mu_unlock(&neighborhood->mu);
  }

  worker_insert(pollset, worker);
  pollset->begin_refs--;
  if (worker->state == UNKICKED && !pollset->kicked_without_poller) {
    CHECK(gpr_atm_no_barrier_load(&g_active_poller) != (gpr_atm)worker);
    worker->initialized_cv = true;
    gpr_cv_init(&worker->cv);
    while (worker->state == UNKICKED && !pollset->shutting_down) {
      GRPC_TRACE_LOG(polling, INFO)
          << "PS:" << pollset << " BEGIN_WAIT:" << worker
          << " kick_state=" << kick_state_string(worker->state)
          << " shutdown=" << pollset->shutting_down;

      if (gpr_cv_wait(&worker->cv, &pollset->mu,
                      deadline.as_timespec(GPR_CLOCK_MONOTONIC)) &&
          worker->state == UNKICKED) {
        // If gpr_cv_wait returns true (i.e a timeout), pretend that the worker
        // received a kick
        SET_KICK_STATE(worker, KICKED);
      }
    }
    grpc_core::ExecCtx::Get()->InvalidateNow();
  }

  GRPC_TRACE_LOG(polling, INFO)
      << "PS:" << pollset << " BEGIN_DONE:" << worker
      << " kick_state=" << kick_state_string(worker->state)
      << " shutdown=" << pollset->shutting_down
      << " kicked_without_poller: " << pollset->kicked_without_poller;

  // We release pollset lock in this function at a couple of places:
  //   1. Briefly when assigning pollset to a neighborhood
  //   2. When doing gpr_cv_wait()
  // It is possible that 'kicked_without_poller' was set to true during (1) and
  // 'shutting_down' is set to true during (1) or (2). If either of them is
  // true, this worker cannot do polling
  // TODO(sreek): Perhaps there is a better way to handle kicked_without_poller
  // case; especially when the worker is the DESIGNATED_POLLER

  if (pollset->kicked_without_poller) {
    pollset->kicked_without_poller = false;
    return false;
  }

  return worker->state == DESIGNATED_POLLER && !pollset->shutting_down;
}

static bool check_neighborhood_for_available_poller(
    pollset_neighborhood* neighborhood) {
  bool found_worker = false;
  do {
    grpc_pollset* inspect = neighborhood->active_root;
    if (inspect == nullptr) {
      break;
    }
    gpr_mu_lock(&inspect->mu);
    CHECK(!inspect->seen_inactive);
    grpc_pollset_worker* inspect_worker = inspect->root_worker;
    if (inspect_worker != nullptr) {
      do {
        switch (inspect_worker->state) {
          case UNKICKED:
            if (gpr_atm_no_barrier_cas(
                    &g_active_poller, 0,
                    reinterpret_cast<gpr_atm>(inspect_worker))) {
              GRPC_TRACE_LOG(polling, INFO)
                  << " .. choose next poller to be " << inspect_worker;
              SET_KICK_STATE(inspect_worker, DESIGNATED_POLLER);
              if (inspect_worker->initialized_cv) {
                gpr_cv_signal(&inspect_worker->cv);
              }
            } else {
              GRPC_TRACE_LOG(polling, INFO)
                  << " .. beaten to choose next poller";
            }
            // even if we didn't win the cas, there's a worker, we can stop
            found_worker = true;
            break;
          case KICKED:
            break;
          case DESIGNATED_POLLER:
            found_worker = true;  // ok, so someone else found the worker, but
                                  // we'll accept that
            break;
        }
        inspect_worker = inspect_worker->next;
      } while (!found_worker && inspect_worker != inspect->root_worker);
    }
    if (!found_worker) {
      GRPC_TRACE_LOG(polling, INFO)
          << " .. mark pollset " << inspect << " inactive";
      inspect->seen_inactive = true;
      if (inspect == neighborhood->active_root) {
        neighborhood->active_root =
            inspect->next == inspect ? nullptr : inspect->next;
      }
      inspect->next->prev = inspect->prev;
      inspect->prev->next = inspect->next;
      inspect->next = inspect->prev = nullptr;
    }
    gpr_mu_unlock(&inspect->mu);
  } while (!found_worker);
  return found_worker;
}

static void end_worker(grpc_pollset* pollset, grpc_pollset_worker* worker,
                       grpc_pollset_worker** worker_hdl) {
  GRPC_TRACE_LOG(polling, INFO) << "PS:" << pollset << " END_WORKER:" << worker;
  if (worker_hdl != nullptr) *worker_hdl = nullptr;
  // Make sure we appear kicked
  SET_KICK_STATE(worker, KICKED);
  grpc_closure_list_move(&worker->schedule_on_end_work,
                         grpc_core::ExecCtx::Get()->closure_list());
  if (gpr_atm_no_barrier_load(&g_active_poller) ==
      reinterpret_cast<gpr_atm>(worker)) {
    if (worker->next != worker && worker->next->state == UNKICKED) {
      GRPC_TRACE_LOG(polling, INFO)
          << " .. choose next poller to be peer " << worker;
      CHECK(worker->next->initialized_cv);
      gpr_atm_no_barrier_store(&g_active_poller, (gpr_atm)worker->next);
      SET_KICK_STATE(worker->next, DESIGNATED_POLLER);
      gpr_cv_signal(&worker->next->cv);
      if (grpc_core::ExecCtx::Get()->HasWork()) {
        gpr_mu_unlock(&pollset->mu);
        grpc_core::ExecCtx::Get()->Flush();
        gpr_mu_lock(&pollset->mu);
      }
    } else {
      gpr_atm_no_barrier_store(&g_active_poller, 0);
      size_t poller_neighborhood_idx =
          static_cast<size_t>(pollset->neighborhood - g_neighborhoods);
      gpr_mu_unlock(&pollset->mu);
      bool found_worker = false;
      bool scan_state[MAX_NEIGHBORHOODS];
      for (size_t i = 0; !found_worker && i < g_num_neighborhoods; i++) {
        pollset_neighborhood* neighborhood =
            &g_neighborhoods[(poller_neighborhood_idx + i) %
                             g_num_neighborhoods];
        if (gpr_mu_trylock(&neighborhood->mu)) {
          found_worker = check_neighborhood_for_available_poller(neighborhood);
          gpr_mu_unlock(&neighborhood->mu);
          scan_state[i] = true;
        } else {
          scan_state[i] = false;
        }
      }
      for (size_t i = 0; !found_worker && i < g_num_neighborhoods; i++) {
        if (scan_state[i]) continue;
        pollset_neighborhood* neighborhood =
            &g_neighborhoods[(poller_neighborhood_idx + i) %
                             g_num_neighborhoods];
        gpr_mu_lock(&neighborhood->mu);
        found_worker = check_neighborhood_for_available_poller(neighborhood);
        gpr_mu_unlock(&neighborhood->mu);
      }
      grpc_core::ExecCtx::Get()->Flush();
      gpr_mu_lock(&pollset->mu);
    }
  } else if (grpc_core::ExecCtx::Get()->HasWork()) {
    gpr_mu_unlock(&pollset->mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(&pollset->mu);
  }
  if (worker->initialized_cv) {
    gpr_cv_destroy(&worker->cv);
  }
  GRPC_TRACE_LOG(polling, INFO) << " .. remove worker";
  if (EMPTIED == worker_remove(pollset, worker)) {
    pollset_maybe_finish_shutdown(pollset);
  }
  CHECK(gpr_atm_no_barrier_load(&g_active_poller) != (gpr_atm)worker);
}

// pollset->po.mu lock must be held by the caller before calling this.
// The function pollset_work() may temporarily release the lock (pollset->po.mu)
// during the course of its execution but it will always re-acquire the lock and
// ensure that it is held by the time the function returns
static grpc_error_handle pollset_work(grpc_pollset* ps,
                                      grpc_pollset_worker** worker_hdl,
                                      grpc_core::Timestamp deadline) {
  grpc_pollset_worker worker;
  grpc_error_handle error;
  static const char* err_desc = "pollset_work";
  if (ps->kicked_without_poller) {
    ps->kicked_without_poller = false;
    return absl::OkStatus();
  }

  if (begin_worker(ps, &worker, worker_hdl, deadline)) {
    g_current_thread_pollset = ps;
    g_current_thread_worker = &worker;
    CHECK(!ps->shutting_down);
    CHECK(!ps->seen_inactive);

    gpr_mu_unlock(&ps->mu);  // unlock
    // This is the designated polling thread at this point and should ideally do
    // polling. However, if there are unprocessed events left from a previous
    // call to do_epoll_wait(), skip calling epoll_wait() in this iteration and
    // process the pending epoll events.

    // The reason for decoupling do_epoll_wait and process_epoll_events is to
    // better distribute the work (i.e handling epoll events) across multiple
    // threads

    // process_epoll_events() returns very quickly: It just queues the work on
    // exec_ctx but does not execute it (the actual exectution or more
    // accurately grpc_core::ExecCtx::Get()->Flush() happens in end_worker()
    // AFTER selecting a designated poller). So we are not waiting long periods
    // without a designated poller
    if (gpr_atm_acq_load(&g_epoll_set.cursor) ==
        gpr_atm_acq_load(&g_epoll_set.num_events)) {
      append_error(&error, do_epoll_wait(ps, deadline), err_desc);
    }
    append_error(&error, process_epoll_events(ps), err_desc);

    gpr_mu_lock(&ps->mu);  // lock

    g_current_thread_worker = nullptr;
  } else {
    g_current_thread_pollset = ps;
  }
  end_worker(ps, &worker, worker_hdl);

  g_current_thread_pollset = nullptr;
  return error;
}

static grpc_error_handle pollset_kick(grpc_pollset* pollset,
                                      grpc_pollset_worker* specific_worker) {
  grpc_error_handle ret_err;
  if (GRPC_TRACE_FLAG_ENABLED(polling)) {
    std::vector<std::string> log;
    log.push_back(absl::StrFormat(
        "PS:%p KICK:%p curps=%p curworker=%p root=%p", pollset, specific_worker,
        static_cast<void*>(g_current_thread_pollset),
        static_cast<void*>(g_current_thread_worker), pollset->root_worker));
    if (pollset->root_worker != nullptr) {
      log.push_back(absl::StrFormat(
          " {kick_state=%s next=%p {kick_state=%s}}",
          kick_state_string(pollset->root_worker->state),
          pollset->root_worker->next,
          kick_state_string(pollset->root_worker->next->state)));
    }
    if (specific_worker != nullptr) {
      log.push_back(absl::StrFormat(" worker_kick_state=%s",
                                    kick_state_string(specific_worker->state)));
    }
    VLOG(2) << absl::StrJoin(log, "");
  }

  if (specific_worker == nullptr) {
    if (g_current_thread_pollset != pollset) {
      grpc_pollset_worker* root_worker = pollset->root_worker;
      if (root_worker == nullptr) {
        pollset->kicked_without_poller = true;
        GRPC_TRACE_LOG(polling, INFO) << " .. kicked_without_poller";
        goto done;
      }
      grpc_pollset_worker* next_worker = root_worker->next;
      if (root_worker->state == KICKED) {
        GRPC_TRACE_LOG(polling, INFO) << " .. already kicked " << root_worker;
        SET_KICK_STATE(root_worker, KICKED);
        goto done;
      } else if (next_worker->state == KICKED) {
        GRPC_TRACE_LOG(polling, INFO) << " .. already kicked " << next_worker;
        SET_KICK_STATE(next_worker, KICKED);
        goto done;
      } else if (root_worker == next_worker &&  // only try and wake up a poller
                                                // if there is no next worker
                 root_worker ==
                     reinterpret_cast<grpc_pollset_worker*>(
                         gpr_atm_no_barrier_load(&g_active_poller))) {
        GRPC_TRACE_LOG(polling, INFO) << " .. kicked " << root_worker;
        SET_KICK_STATE(root_worker, KICKED);
        ret_err = grpc_wakeup_fd_wakeup(&global_wakeup_fd);
        goto done;
      } else if (next_worker->state == UNKICKED) {
        GRPC_TRACE_LOG(polling, INFO) << " .. kicked " << next_worker;
        CHECK(next_worker->initialized_cv);
        SET_KICK_STATE(next_worker, KICKED);
        gpr_cv_signal(&next_worker->cv);
        goto done;
      } else if (next_worker->state == DESIGNATED_POLLER) {
        if (root_worker->state != DESIGNATED_POLLER) {
          GRPC_TRACE_LOG(polling, INFO)
              << " .. kicked root non-poller " << root_worker
              << " (initialized_cv=" << root_worker->initialized_cv
              << ") (poller=" << next_worker << ")";
          SET_KICK_STATE(root_worker, KICKED);
          if (root_worker->initialized_cv) {
            gpr_cv_signal(&root_worker->cv);
          }
          goto done;
        } else {
          GRPC_TRACE_LOG(polling, INFO) << " .. non-root poller " << next_worker
                                        << " (root=" << root_worker << ")";
          SET_KICK_STATE(next_worker, KICKED);
          ret_err = grpc_wakeup_fd_wakeup(&global_wakeup_fd);
          goto done;
        }
      } else {
        CHECK(next_worker->state == KICKED);
        SET_KICK_STATE(next_worker, KICKED);
        goto done;
      }
    } else {
      GRPC_TRACE_LOG(polling, INFO) << " .. kicked while waking up";
      goto done;
    }

    GPR_UNREACHABLE_CODE(goto done);
  }

  if (specific_worker->state == KICKED) {
    GRPC_TRACE_LOG(polling, INFO) << " .. specific worker already kicked";
    goto done;
  } else if (g_current_thread_worker == specific_worker) {
    GRPC_TRACE_LOG(polling, INFO)
        << " .. mark " << specific_worker << " kicked";
    SET_KICK_STATE(specific_worker, KICKED);
    goto done;
  } else if (specific_worker ==
             reinterpret_cast<grpc_pollset_worker*>(
                 gpr_atm_no_barrier_load(&g_active_poller))) {
    GRPC_TRACE_LOG(polling, INFO) << " .. kick active poller";
    SET_KICK_STATE(specific_worker, KICKED);
    ret_err = grpc_wakeup_fd_wakeup(&global_wakeup_fd);
    goto done;
  } else if (specific_worker->initialized_cv) {
    GRPC_TRACE_LOG(polling, INFO) << " .. kick waiting worker";
    SET_KICK_STATE(specific_worker, KICKED);
    gpr_cv_signal(&specific_worker->cv);
    goto done;
  } else {
    GRPC_TRACE_LOG(polling, INFO) << " .. kick non-waiting worker";
    SET_KICK_STATE(specific_worker, KICKED);
    goto done;
  }
done:
  return ret_err;
}

static void pollset_add_fd(grpc_pollset* /*pollset*/, grpc_fd* /*fd*/) {}

//******************************************************************************
// Pollset-set Definitions
//

static grpc_pollset_set* pollset_set_create(void) {
  return reinterpret_cast<grpc_pollset_set*>(static_cast<intptr_t>(0xdeafbeef));
}

static void pollset_set_destroy(grpc_pollset_set* /*pss*/) {}

static void pollset_set_add_fd(grpc_pollset_set* /*pss*/, grpc_fd* /*fd*/) {}

static void pollset_set_del_fd(grpc_pollset_set* /*pss*/, grpc_fd* /*fd*/) {}

static void pollset_set_add_pollset(grpc_pollset_set* /*pss*/,
                                    grpc_pollset* /*ps*/) {}

static void pollset_set_del_pollset(grpc_pollset_set* /*pss*/,
                                    grpc_pollset* /*ps*/) {}

static void pollset_set_add_pollset_set(grpc_pollset_set* /*bag*/,
                                        grpc_pollset_set* /*item*/) {}

static void pollset_set_del_pollset_set(grpc_pollset_set* /*bag*/,
                                        grpc_pollset_set* /*item*/) {}

//******************************************************************************
// Event engine binding
//

static bool is_any_background_poller_thread(void) { return false; }

static void shutdown_background_closure(void) {}

static bool add_closure_to_background_poller(grpc_closure* /*closure*/,
                                             grpc_error_handle /*error*/) {
  return false;
}

static void shutdown_engine(void) {
  fd_global_shutdown();
  pollset_global_shutdown();
  epoll_set_shutdown();
  g_is_shutdown = true;
}

static bool init_epoll1_linux();

const grpc_event_engine_vtable grpc_ev_epoll1_posix = {
    sizeof(grpc_pollset),
    true,
    false,

    fd_create,
    fd_wrapped_fd,
    fd_orphan,
    fd_shutdown,
    fd_notify_on_read,
    fd_notify_on_write,
    fd_notify_on_error,
    fd_become_readable,
    fd_become_writable,
    fd_has_errors,
    fd_is_shutdown,

    pollset_init,
    pollset_shutdown,
    pollset_destroy,
    pollset_work,
    pollset_kick,
    pollset_add_fd,

    pollset_set_create,
    pollset_set_destroy,
    pollset_set_add_pollset,
    pollset_set_del_pollset,
    pollset_set_add_pollset_set,
    pollset_set_del_pollset_set,
    pollset_set_add_fd,
    pollset_set_del_fd,

    is_any_background_poller_thread,
    /* name = */ "epoll1",
    /* check_engine_available = */
    [](bool) { return init_epoll1_linux(); },
    /* init_engine = */
    []() { CHECK(init_epoll1_linux()); },
    shutdown_background_closure,
    /* shutdown_engine = */
    []() { shutdown_engine(); },
    add_closure_to_background_poller,

    fd_set_pre_allocated,
};

// Called by the child process's post-fork handler to close open fds, including
// the global epoll fd. This allows gRPC to shutdown in the child process
// without interfering with connections or RPCs ongoing in the parent.
static void reset_event_manager_on_fork() {
  if (g_is_shutdown) return;
  gpr_mu_lock(&fork_fd_list_mu);
  while (fork_fd_list_head != nullptr) {
    close(fork_fd_list_head->fd);
    fork_fd_list_head->fd = -1;
    fork_fd_list_head = fork_fd_list_head->fork_fd_list->next;
  }
  gpr_mu_unlock(&fork_fd_list_mu);
  shutdown_engine();
  init_epoll1_linux();
}

// It is possible that GLIBC has epoll but the underlying kernel doesn't.
// Create epoll_fd (epoll_set_init() takes care of that) to make sure epoll
// support is available
static bool init_epoll1_linux() {
  if (!g_is_shutdown) return true;
  if (!grpc_has_wakeup_fd()) {
    LOG(ERROR) << "Skipping epoll1 because of no wakeup fd.";
    return false;
  }

  if (!epoll_set_init()) {
    return false;
  }

  fd_global_init();

  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    fd_global_shutdown();
    epoll_set_shutdown();
    return false;
  }

  if (grpc_core::Fork::Enabled()) {
    if (grpc_core::Fork::RegisterResetChildPollingEngineFunc(
            reset_event_manager_on_fork)) {
      gpr_mu_init(&fork_fd_list_mu);
    }
  }
  g_is_shutdown = false;
  return true;
}

#else  // defined(GRPC_LINUX_EPOLL)
#if defined(GRPC_POSIX_SOCKET_EV_EPOLL1)
#include "src/core/lib/iomgr/ev_epoll1_linux.h"
const grpc_event_engine_vtable grpc_ev_epoll1_posix = {
    1,
    true,
    false,

    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,

    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,

    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,

    nullptr,
    /* name = */ "epoll1",
    /* check_engine_available = */ [](bool) { return false; },
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};
#endif  // defined(GRPC_POSIX_SOCKET_EV_EPOLL1)
#endif  // !defined(GRPC_LINUX_EPOLL)
