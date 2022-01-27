/*
 *
 * Copyright 2016 gRPC authors.
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

#if GRPC_ARES == 1

#include <string.h>
#include <sys/types.h>

#include <address_sorting/address_sorting.h>
#include <ares.h>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/nameser.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/timer.h"

using grpc_core::ServerAddress;
using grpc_core::ServerAddressList;

grpc_core::TraceFlag grpc_trace_cares_address_sorting(false,
                                                      "cares_address_sorting");

grpc_core::TraceFlag grpc_trace_cares_resolver(false, "cares_resolver");

typedef struct fd_node {
  /* default constructor exists only for linked list manipulation */
  fd_node() : ev_driver(nullptr) {}

  explicit fd_node(grpc_ares_ev_driver* ev_driver) : ev_driver(ev_driver) {}

  /** the owner of this fd node */
  grpc_ares_ev_driver* const ev_driver;
  /** a closure wrapping on_readable_locked, which should be
     invoked when the grpc_fd in this node becomes readable. */
  grpc_closure read_closure ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** a closure wrapping on_writable_locked, which should be
     invoked when the grpc_fd in this node becomes writable. */
  grpc_closure write_closure ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** next fd node in the list */
  struct fd_node* next ABSL_GUARDED_BY(&grpc_ares_request::mu);

  /** wrapped fd that's polled by grpc's poller for the current platform */
  grpc_core::GrpcPolledFd* grpc_polled_fd
      ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** if the readable closure has been registered */
  bool readable_registered ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** if the writable closure has been registered */
  bool writable_registered ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** if the fd has been shutdown yet from grpc iomgr perspective */
  bool already_shutdown ABSL_GUARDED_BY(&grpc_ares_request::mu);
} fd_node;

struct grpc_ares_ev_driver {
  explicit grpc_ares_ev_driver(grpc_ares_request* request) : request(request) {}

  /** the ares_channel owned by this event driver */
  ares_channel channel ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** pollset set for driving the IO events of the channel */
  grpc_pollset_set* pollset_set ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** refcount of the event driver */
  gpr_refcount refs;

  /** a list of grpc_fd that this event driver is currently using. */
  fd_node* fds ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** is this event driver being shut down */
  bool shutting_down ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** request object that's using this ev driver */
  grpc_ares_request* const request;
  /** Owned by the ev_driver. Creates new GrpcPolledFd's */
  std::unique_ptr<grpc_core::GrpcPolledFdFactory> polled_fd_factory
      ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** query timeout in milliseconds */
  int query_timeout_ms ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** alarm to cancel active queries */
  grpc_timer query_timeout ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** cancels queries on a timeout */
  grpc_closure on_timeout_locked ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** alarm to poll ares_process on in case fd events don't happen */
  grpc_timer ares_backup_poll_alarm ABSL_GUARDED_BY(&grpc_ares_request::mu);
  /** polls ares_process on a periodic timer */
  grpc_closure on_ares_backup_poll_alarm_locked
      ABSL_GUARDED_BY(&grpc_ares_request::mu);
};

// TODO(apolcyn): make grpc_ares_hostbyname_request a sub-class
// of GrpcAresQuery.
typedef struct grpc_ares_hostbyname_request {
  /** following members are set in create_hostbyname_request_locked
   */
  /** the top-level request instance */
  grpc_ares_request* parent_request;
  /** host to resolve, parsed from the name to resolve */
  char* host;
  /** port to fill in sockaddr_in, parsed from the name to resolve */
  uint16_t port;
  /** is it a grpclb address */
  bool is_balancer;
  /** for logging and errors: the query type ("A" or "AAAA") */
  const char* qtype;
} grpc_ares_hostbyname_request;

static void grpc_ares_request_ref_locked(grpc_ares_request* r)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(r->mu);
static void grpc_ares_request_unref_locked(grpc_ares_request* r)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(r->mu);

// TODO(apolcyn): as a part of C++-ification, find a way to
// organize per-query and per-resolution information in such a way
// that doesn't involve allocating a number of different data
// structures.
class GrpcAresQuery {
 public:
  explicit GrpcAresQuery(grpc_ares_request* r, const std::string& name)
      : r_(r), name_(name) {
    grpc_ares_request_ref_locked(r_);
  }

  ~GrpcAresQuery() { grpc_ares_request_unref_locked(r_); }

  grpc_ares_request* parent_request() { return r_; }

  const std::string& name() { return name_; }

 private:
  /* the top level request instance */
  grpc_ares_request* r_;
  /** for logging and errors */
  const std::string name_;
};

static grpc_ares_ev_driver* grpc_ares_ev_driver_ref(
    grpc_ares_ev_driver* ev_driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  GRPC_CARES_TRACE_LOG("request:%p Ref ev_driver %p", ev_driver->request,
                       ev_driver);
  gpr_ref(&ev_driver->refs);
  return ev_driver;
}

static void grpc_ares_complete_request_locked(grpc_ares_request* r)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(r->mu);

static void grpc_ares_ev_driver_unref(grpc_ares_ev_driver* ev_driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  GRPC_CARES_TRACE_LOG("request:%p Unref ev_driver %p", ev_driver->request,
                       ev_driver);
  if (gpr_unref(&ev_driver->refs)) {
    GRPC_CARES_TRACE_LOG("request:%p destroy ev_driver %p", ev_driver->request,
                         ev_driver);
    GPR_ASSERT(ev_driver->fds == nullptr);
    ares_destroy(ev_driver->channel);
    grpc_ares_complete_request_locked(ev_driver->request);
    delete ev_driver;
  }
}

static void fd_node_destroy_locked(fd_node* fdn)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  GRPC_CARES_TRACE_LOG("request:%p delete fd: %s", fdn->ev_driver->request,
                       fdn->grpc_polled_fd->GetName());
  GPR_ASSERT(!fdn->readable_registered);
  GPR_ASSERT(!fdn->writable_registered);
  GPR_ASSERT(fdn->already_shutdown);
  delete fdn->grpc_polled_fd;
  delete fdn;
}

static void fd_node_shutdown_locked(fd_node* fdn, const char* reason)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  if (!fdn->already_shutdown) {
    fdn->already_shutdown = true;
    fdn->grpc_polled_fd->ShutdownLocked(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(reason));
  }
}

void grpc_ares_ev_driver_on_queries_complete_locked(
    grpc_ares_ev_driver* ev_driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  // We mark the event driver as being shut down.
  // grpc_ares_notify_on_event_locked will shut down any remaining
  // fds.
  ev_driver->shutting_down = true;
  grpc_timer_cancel(&ev_driver->query_timeout);
  grpc_timer_cancel(&ev_driver->ares_backup_poll_alarm);
  grpc_ares_ev_driver_unref(ev_driver);
}

void grpc_ares_ev_driver_shutdown_locked(grpc_ares_ev_driver* ev_driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  ev_driver->shutting_down = true;
  fd_node* fn = ev_driver->fds;
  while (fn != nullptr) {
    fd_node_shutdown_locked(fn, "grpc_ares_ev_driver_shutdown");
    fn = fn->next;
  }
}

// Search fd in the fd_node list head. This is an O(n) search, the max possible
// value of n is ARES_GETSOCK_MAXNUM (16). n is typically 1 - 2 in our tests.
static fd_node* pop_fd_node_locked(fd_node** head, ares_socket_t as)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  fd_node phony_head;
  phony_head.next = *head;
  fd_node* node = &phony_head;
  while (node->next != nullptr) {
    if (node->next->grpc_polled_fd->GetWrappedAresSocketLocked() == as) {
      fd_node* ret = node->next;
      node->next = node->next->next;
      *head = phony_head.next;
      return ret;
    }
    node = node->next;
  }
  return nullptr;
}

static grpc_millis calculate_next_ares_backup_poll_alarm_ms(
    grpc_ares_ev_driver* driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  // An alternative here could be to use ares_timeout to try to be more
  // accurate, but that would require using "struct timeval"'s, which just makes
  // things a bit more complicated. So just poll every second, as suggested
  // by the c-ares code comments.
  grpc_millis ms_until_next_ares_backup_poll_alarm = 1000;
  GRPC_CARES_TRACE_LOG(
      "request:%p ev_driver=%p. next ares process poll time in "
      "%" PRId64 " ms",
      driver->request, driver, ms_until_next_ares_backup_poll_alarm);
  return ms_until_next_ares_backup_poll_alarm +
         grpc_core::ExecCtx::Get()->Now();
}

static void on_timeout(void* arg, grpc_error_handle error) {
  grpc_ares_ev_driver* driver = static_cast<grpc_ares_ev_driver*>(arg);
  grpc_core::MutexLock lock(&driver->request->mu);
  GRPC_CARES_TRACE_LOG(
      "request:%p ev_driver=%p on_timeout_locked. driver->shutting_down=%d. "
      "err=%s",
      driver->request, driver, driver->shutting_down,
      grpc_error_std_string(error).c_str());
  if (!driver->shutting_down && error == GRPC_ERROR_NONE) {
    grpc_ares_ev_driver_shutdown_locked(driver);
  }
  grpc_ares_ev_driver_unref(driver);
}

static void grpc_ares_notify_on_event_locked(grpc_ares_ev_driver* ev_driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu);

/* In case of non-responsive DNS servers, dropped packets, etc., c-ares has
 * intelligent timeout and retry logic, which we can take advantage of by
 * polling ares_process_fd on time intervals. Overall, the c-ares library is
 * meant to be called into and given a chance to proceed name resolution:
 *   a) when fd events happen
 *   b) when some time has passed without fd events having happened
 * For the latter, we use this backup poller. Also see
 * https://github.com/grpc/grpc/pull/17688 description for more details. */
static void on_ares_backup_poll_alarm(void* arg, grpc_error_handle error) {
  grpc_ares_ev_driver* driver = static_cast<grpc_ares_ev_driver*>(arg);
  grpc_core::MutexLock lock(&driver->request->mu);
  GRPC_CARES_TRACE_LOG(
      "request:%p ev_driver=%p on_ares_backup_poll_alarm_locked. "
      "driver->shutting_down=%d. "
      "err=%s",
      driver->request, driver, driver->shutting_down,
      grpc_error_std_string(error).c_str());
  if (!driver->shutting_down && error == GRPC_ERROR_NONE) {
    fd_node* fdn = driver->fds;
    while (fdn != nullptr) {
      if (!fdn->already_shutdown) {
        GRPC_CARES_TRACE_LOG(
            "request:%p ev_driver=%p on_ares_backup_poll_alarm_locked; "
            "ares_process_fd. fd=%s",
            driver->request, driver, fdn->grpc_polled_fd->GetName());
        ares_socket_t as = fdn->grpc_polled_fd->GetWrappedAresSocketLocked();
        ares_process_fd(driver->channel, as, as);
      }
      fdn = fdn->next;
    }
    if (!driver->shutting_down) {
      // InvalidateNow to avoid getting stuck re-initializing this timer
      // in a loop while draining the currently-held WorkSerializer.
      // Also see https://github.com/grpc/grpc/issues/26079.
      grpc_core::ExecCtx::Get()->InvalidateNow();
      grpc_millis next_ares_backup_poll_alarm =
          calculate_next_ares_backup_poll_alarm_ms(driver);
      grpc_ares_ev_driver_ref(driver);
      GRPC_CLOSURE_INIT(&driver->on_ares_backup_poll_alarm_locked,
                        on_ares_backup_poll_alarm, driver,
                        grpc_schedule_on_exec_ctx);
      grpc_timer_init(&driver->ares_backup_poll_alarm,
                      next_ares_backup_poll_alarm,
                      &driver->on_ares_backup_poll_alarm_locked);
    }
    grpc_ares_notify_on_event_locked(driver);
  }
  grpc_ares_ev_driver_unref(driver);
}

static void on_readable(void* arg, grpc_error_handle error) {
  fd_node* fdn = static_cast<fd_node*>(arg);
  grpc_core::MutexLock lock(&fdn->ev_driver->request->mu);
  GPR_ASSERT(fdn->readable_registered);
  grpc_ares_ev_driver* ev_driver = fdn->ev_driver;
  const ares_socket_t as = fdn->grpc_polled_fd->GetWrappedAresSocketLocked();
  fdn->readable_registered = false;
  GRPC_CARES_TRACE_LOG("request:%p readable on %s", fdn->ev_driver->request,
                       fdn->grpc_polled_fd->GetName());
  if (error == GRPC_ERROR_NONE) {
    do {
      ares_process_fd(ev_driver->channel, as, ARES_SOCKET_BAD);
    } while (fdn->grpc_polled_fd->IsFdStillReadableLocked());
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this ev_driver will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be invoked
    // with a status of ARES_ECANCELLED. The remaining file descriptors in this
    // ev_driver will be cleaned up in the follwing
    // grpc_ares_notify_on_event_locked().
    ares_cancel(ev_driver->channel);
  }
  grpc_ares_notify_on_event_locked(ev_driver);
  grpc_ares_ev_driver_unref(ev_driver);
}

static void on_writable(void* arg, grpc_error_handle error) {
  fd_node* fdn = static_cast<fd_node*>(arg);
  grpc_core::MutexLock lock(&fdn->ev_driver->request->mu);
  GPR_ASSERT(fdn->writable_registered);
  grpc_ares_ev_driver* ev_driver = fdn->ev_driver;
  const ares_socket_t as = fdn->grpc_polled_fd->GetWrappedAresSocketLocked();
  fdn->writable_registered = false;
  GRPC_CARES_TRACE_LOG("request:%p writable on %s", ev_driver->request,
                       fdn->grpc_polled_fd->GetName());
  if (error == GRPC_ERROR_NONE) {
    ares_process_fd(ev_driver->channel, ARES_SOCKET_BAD, as);
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this ev_driver will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be invoked
    // with a status of ARES_ECANCELLED. The remaining file descriptors in this
    // ev_driver will be cleaned up in the follwing
    // grpc_ares_notify_on_event_locked().
    ares_cancel(ev_driver->channel);
  }
  grpc_ares_notify_on_event_locked(ev_driver);
  grpc_ares_ev_driver_unref(ev_driver);
}

// Get the file descriptors used by the ev_driver's ares channel, register
// driver_closure with these filedescriptors.
static void grpc_ares_notify_on_event_locked(grpc_ares_ev_driver* ev_driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  fd_node* new_list = nullptr;
  if (!ev_driver->shutting_down) {
    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int socks_bitmask =
        ares_getsock(ev_driver->channel, socks, ARES_GETSOCK_MAXNUM);
    for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
          ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
        fd_node* fdn = pop_fd_node_locked(&ev_driver->fds, socks[i]);
        // Create a new fd_node if sock[i] is not in the fd_node list.
        if (fdn == nullptr) {
          fdn = new fd_node(ev_driver);
          fdn->grpc_polled_fd =
              ev_driver->polled_fd_factory->NewGrpcPolledFdLocked(
                  socks[i], ev_driver->pollset_set);
          GRPC_CARES_TRACE_LOG("request:%p new fd: %s", ev_driver->request,
                               fdn->grpc_polled_fd->GetName());
          fdn->readable_registered = false;
          fdn->writable_registered = false;
          fdn->already_shutdown = false;
        }
        fdn->next = new_list;
        new_list = fdn;
        // Register read_closure if the socket is readable and read_closure has
        // not been registered with this socket.
        if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
            !fdn->readable_registered) {
          grpc_ares_ev_driver_ref(ev_driver);
          GRPC_CARES_TRACE_LOG("request:%p notify read on: %s",
                               ev_driver->request,
                               fdn->grpc_polled_fd->GetName());
          GRPC_CLOSURE_INIT(&fdn->read_closure, on_readable, fdn,
                            grpc_schedule_on_exec_ctx);
          fdn->grpc_polled_fd->RegisterForOnReadableLocked(&fdn->read_closure);
          fdn->readable_registered = true;
        }
        // Register write_closure if the socket is writable and write_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
            !fdn->writable_registered) {
          GRPC_CARES_TRACE_LOG("request:%p notify write on: %s",
                               ev_driver->request,
                               fdn->grpc_polled_fd->GetName());
          grpc_ares_ev_driver_ref(ev_driver);
          GRPC_CLOSURE_INIT(&fdn->write_closure, on_writable, fdn,
                            grpc_schedule_on_exec_ctx);
          GRPC_CLOSURE_INIT(&fdn->write_closure, on_writable, fdn,
                            grpc_schedule_on_exec_ctx);
          fdn->grpc_polled_fd->RegisterForOnWriteableLocked(
              &fdn->write_closure);
          fdn->writable_registered = true;
        }
      }
    }
  }
  // Any remaining fds in ev_driver->fds were not returned by ares_getsock() and
  // are therefore no longer in use, so they can be shut down and removed from
  // the list.
  while (ev_driver->fds != nullptr) {
    fd_node* cur = ev_driver->fds;
    ev_driver->fds = ev_driver->fds->next;
    fd_node_shutdown_locked(cur, "c-ares fd shutdown");
    if (!cur->readable_registered && !cur->writable_registered) {
      fd_node_destroy_locked(cur);
    } else {
      cur->next = new_list;
      new_list = cur;
    }
  }
  ev_driver->fds = new_list;
}

void grpc_ares_ev_driver_start_locked(grpc_ares_ev_driver* ev_driver)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) {
  grpc_ares_notify_on_event_locked(ev_driver);
  // Initialize overall DNS resolution timeout alarm
  grpc_millis timeout =
      ev_driver->query_timeout_ms == 0
          ? GRPC_MILLIS_INF_FUTURE
          : ev_driver->query_timeout_ms + grpc_core::ExecCtx::Get()->Now();
  GRPC_CARES_TRACE_LOG(
      "request:%p ev_driver=%p grpc_ares_ev_driver_start_locked. timeout in "
      "%" PRId64 " ms",
      ev_driver->request, ev_driver, timeout);
  grpc_ares_ev_driver_ref(ev_driver);
  GRPC_CLOSURE_INIT(&ev_driver->on_timeout_locked, on_timeout, ev_driver,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&ev_driver->query_timeout, timeout,
                  &ev_driver->on_timeout_locked);
  // Initialize the backup poll alarm
  grpc_millis next_ares_backup_poll_alarm =
      calculate_next_ares_backup_poll_alarm_ms(ev_driver);
  grpc_ares_ev_driver_ref(ev_driver);
  GRPC_CLOSURE_INIT(&ev_driver->on_ares_backup_poll_alarm_locked,
                    on_ares_backup_poll_alarm, ev_driver,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&ev_driver->ares_backup_poll_alarm,
                  next_ares_backup_poll_alarm,
                  &ev_driver->on_ares_backup_poll_alarm_locked);
}

static void noop_inject_channel_config(ares_channel /*channel*/) {}

void (*grpc_ares_test_only_inject_config)(ares_channel channel) =
    noop_inject_channel_config;

grpc_error_handle grpc_ares_ev_driver_create_locked(
    grpc_ares_ev_driver** ev_driver, grpc_pollset_set* pollset_set,
    int query_timeout_ms, grpc_ares_request* request)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(request->mu) {
  *ev_driver = new grpc_ares_ev_driver(request);
  ares_options opts;
  memset(&opts, 0, sizeof(opts));
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&(*ev_driver)->channel, &opts, ARES_OPT_FLAGS);
  grpc_ares_test_only_inject_config((*ev_driver)->channel);
  GRPC_CARES_TRACE_LOG("request:%p grpc_ares_ev_driver_create_locked", request);
  if (status != ARES_SUCCESS) {
    grpc_error_handle err = GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
        "Failed to init ares channel. C-ares error: ", ares_strerror(status)));
    delete *ev_driver;
    return err;
  }
  gpr_ref_init(&(*ev_driver)->refs, 1);
  (*ev_driver)->pollset_set = pollset_set;
  (*ev_driver)->fds = nullptr;
  (*ev_driver)->shutting_down = false;
  (*ev_driver)->polled_fd_factory =
      grpc_core::NewGrpcPolledFdFactory(&(*ev_driver)->request->mu);
  (*ev_driver)
      ->polled_fd_factory->ConfigureAresChannelLocked((*ev_driver)->channel);
  (*ev_driver)->query_timeout_ms = query_timeout_ms;
  return GRPC_ERROR_NONE;
}

static void log_address_sorting_list(const grpc_ares_request* r,
                                     const ServerAddressList& addresses,
                                     const char* input_output_str) {
  for (size_t i = 0; i < addresses.size(); i++) {
    std::string addr_str =
        grpc_sockaddr_to_string(&addresses[i].address(), true);
    gpr_log(GPR_INFO,
            "(c-ares resolver) request:%p c-ares address sorting: %s[%" PRIuPTR
            "]=%s",
            r, input_output_str, i, addr_str.c_str());
  }
}

void grpc_cares_wrapper_address_sorting_sort(const grpc_ares_request* r,
                                             ServerAddressList* addresses) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    log_address_sorting_list(r, *addresses, "input");
  }
  address_sorting_sortable* sortables = static_cast<address_sorting_sortable*>(
      gpr_zalloc(sizeof(address_sorting_sortable) * addresses->size()));
  for (size_t i = 0; i < addresses->size(); ++i) {
    sortables[i].user_data = &(*addresses)[i];
    memcpy(&sortables[i].dest_addr.addr, &(*addresses)[i].address().addr,
           (*addresses)[i].address().len);
    sortables[i].dest_addr.len = (*addresses)[i].address().len;
  }
  address_sorting_rfc_6724_sort(sortables, addresses->size());
  ServerAddressList sorted;
  sorted.reserve(addresses->size());
  for (size_t i = 0; i < addresses->size(); ++i) {
    sorted.emplace_back(*static_cast<ServerAddress*>(sortables[i].user_data));
  }
  gpr_free(sortables);
  *addresses = std::move(sorted);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    log_address_sorting_list(r, *addresses, "output");
  }
}

static void grpc_ares_request_ref_locked(grpc_ares_request* r)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(r->mu) {
  r->pending_queries++;
}

static void grpc_ares_request_unref_locked(grpc_ares_request* r)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(r->mu) {
  r->pending_queries--;
  if (r->pending_queries == 0u) {
    grpc_ares_ev_driver_on_queries_complete_locked(r->ev_driver);
  }
}

void grpc_ares_complete_request_locked(grpc_ares_request* r)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(r->mu) {
  /* Invoke on_done callback and destroy the
     request */
  r->ev_driver = nullptr;
  ServerAddressList* addresses = r->addresses_out->get();
  if (addresses != nullptr) {
    grpc_cares_wrapper_address_sorting_sort(r, addresses);
    GRPC_ERROR_UNREF(r->error);
    r->error = GRPC_ERROR_NONE;
    // TODO(apolcyn): allow c-ares to return a service config
    // with no addresses along side it
  }
  if (r->balancer_addresses_out != nullptr) {
    ServerAddressList* balancer_addresses = r->balancer_addresses_out->get();
    if (balancer_addresses != nullptr) {
      grpc_cares_wrapper_address_sorting_sort(r, balancer_addresses);
    }
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done, r->error);
}

/* Note that the returned object takes a reference to qtype, so
 * qtype must outlive it. */
static grpc_ares_hostbyname_request* create_hostbyname_request_locked(
    grpc_ares_request* parent_request, const char* host, uint16_t port,
    bool is_balancer, const char* qtype)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_request->mu) {
  GRPC_CARES_TRACE_LOG(
      "request:%p create_hostbyname_request_locked host:%s port:%d "
      "is_balancer:%d qtype:%s",
      parent_request, host, port, is_balancer, qtype);
  grpc_ares_hostbyname_request* hr = new grpc_ares_hostbyname_request();
  hr->parent_request = parent_request;
  hr->host = gpr_strdup(host);
  hr->port = port;
  hr->is_balancer = is_balancer;
  hr->qtype = qtype;
  grpc_ares_request_ref_locked(parent_request);
  return hr;
}

static void destroy_hostbyname_request_locked(grpc_ares_hostbyname_request* hr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(hr->parent_request->mu) {
  grpc_ares_request_unref_locked(hr->parent_request);
  gpr_free(hr->host);
  delete hr;
}

static void on_hostbyname_done_locked(void* arg, int status, int /*timeouts*/,
                                      struct hostent* hostent)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  grpc_ares_hostbyname_request* hr =
      static_cast<grpc_ares_hostbyname_request*>(arg);
  grpc_ares_request* r = hr->parent_request;
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG(
        "request:%p on_hostbyname_done_locked qtype=%s host=%s ARES_SUCCESS", r,
        hr->qtype, hr->host);
    std::unique_ptr<ServerAddressList>* address_list_ptr =
        hr->is_balancer ? r->balancer_addresses_out : r->addresses_out;
    if (*address_list_ptr == nullptr) {
      *address_list_ptr = absl::make_unique<ServerAddressList>();
    }
    ServerAddressList& addresses = **address_list_ptr;
    for (size_t i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
      absl::InlinedVector<grpc_arg, 1> args_to_add;
      if (hr->is_balancer) {
        args_to_add.emplace_back(grpc_channel_arg_string_create(
            const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY), hr->host));
      }
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, args_to_add.data(), args_to_add.size());
      switch (hostent->h_addrtype) {
        case AF_INET6: {
          size_t addr_len = sizeof(struct sockaddr_in6);
          struct sockaddr_in6 addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin6_addr, hostent->h_addr_list[i],
                 sizeof(struct in6_addr));
          addr.sin6_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin6_port = hr->port;
          addresses.emplace_back(&addr, addr_len, args);
          char output[INET6_ADDRSTRLEN];
          ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
          GRPC_CARES_TRACE_LOG(
              "request:%p c-ares resolver gets a AF_INET6 result: \n"
              "  addr: %s\n  port: %d\n  sin6_scope_id: %d\n",
              r, output, ntohs(hr->port), addr.sin6_scope_id);
          break;
        }
        case AF_INET: {
          size_t addr_len = sizeof(struct sockaddr_in);
          struct sockaddr_in addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin_addr, hostent->h_addr_list[i],
                 sizeof(struct in_addr));
          addr.sin_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin_port = hr->port;
          addresses.emplace_back(&addr, addr_len, args);
          char output[INET_ADDRSTRLEN];
          ares_inet_ntop(AF_INET, &addr.sin_addr, output, INET_ADDRSTRLEN);
          GRPC_CARES_TRACE_LOG(
              "request:%p c-ares resolver gets a AF_INET result: \n"
              "  addr: %s\n  port: %d\n",
              r, output, ntohs(hr->port));
          break;
        }
      }
    }
  } else {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=%s name=%s is_balancer=%d: %s",
        hr->qtype, hr->host, hr->is_balancer, ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_hostbyname_done_locked: %s", r,
                         error_msg.c_str());
    grpc_error_handle error =
        GRPC_ERROR_CREATE_FROM_CPP_STRING(std::move(error_msg));
    r->error = grpc_error_add_child(error, r->error);
  }
  destroy_hostbyname_request_locked(hr);
}

static void on_srv_query_done_locked(void* arg, int status, int /*timeouts*/,
                                     unsigned char* abuf,
                                     int alen) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  GrpcAresQuery* q = static_cast<GrpcAresQuery*>(arg);
  grpc_ares_request* r = q->parent_request();
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG(
        "request:%p on_srv_query_done_locked name=%s ARES_SUCCESS", r,
        q->name().c_str());
    struct ares_srv_reply* reply;
    const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
    GRPC_CARES_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r,
                         parse_status);
    if (parse_status == ARES_SUCCESS) {
      for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
           srv_it = srv_it->next) {
        if (grpc_ares_query_ipv6()) {
          grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
              r, srv_it->host, htons(srv_it->port), true /* is_balancer */,
              "AAAA");
          ares_gethostbyname(r->ev_driver->channel, hr->host, AF_INET6,
                             on_hostbyname_done_locked, hr);
        }
        grpc_ares_hostbyname_request* hr = create_hostbyname_request_locked(
            r, srv_it->host, htons(srv_it->port), true /* is_balancer */, "A");
        ares_gethostbyname(r->ev_driver->channel, hr->host, AF_INET,
                           on_hostbyname_done_locked, hr);
        grpc_ares_notify_on_event_locked(r->ev_driver);
      }
    }
    if (reply != nullptr) {
      ares_free_data(reply);
    }
  } else {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=SRV name=%s: %s", q->name(),
        ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_srv_query_done_locked: %s", r,
                         error_msg.c_str());
    grpc_error_handle error =
        GRPC_ERROR_CREATE_FROM_CPP_STRING(std::move(error_msg));
    r->error = grpc_error_add_child(error, r->error);
  }
  delete q;
}

static const char g_service_config_attribute_prefix[] = "grpc_config=";

static void on_txt_done_locked(void* arg, int status, int /*timeouts*/,
                               unsigned char* buf,
                               int len) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  // This callback is invoked from the c-ares library, so disable thread safety
  // analysis. Note that we are guaranteed to be holding r->mu, though.
  GrpcAresQuery* q = static_cast<GrpcAresQuery*>(arg);
  std::unique_ptr<GrpcAresQuery> query_deleter(q);
  grpc_ares_request* r = q->parent_request();
  const size_t prefix_len = sizeof(g_service_config_attribute_prefix) - 1;
  struct ares_txt_ext* result = nullptr;
  struct ares_txt_ext* reply = nullptr;
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (status != ARES_SUCCESS) goto fail;
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked name=%s ARES_SUCCESS", r,
                       q->name().c_str());
  status = ares_parse_txt_reply_ext(buf, len, &reply);
  if (status != ARES_SUCCESS) goto fail;
  // Find service config in TXT record.
  for (result = reply; result != nullptr; result = result->next) {
    if (result->record_start &&
        memcmp(result->txt, g_service_config_attribute_prefix, prefix_len) ==
            0) {
      break;
    }
  }
  // Found a service config record.
  if (result != nullptr) {
    size_t service_config_len = result->length - prefix_len;
    *r->service_config_json_out =
        static_cast<char*>(gpr_malloc(service_config_len + 1));
    memcpy(*r->service_config_json_out, result->txt + prefix_len,
           service_config_len);
    for (result = result->next; result != nullptr && !result->record_start;
         result = result->next) {
      *r->service_config_json_out = static_cast<char*>(
          gpr_realloc(*r->service_config_json_out,
                      service_config_len + result->length + 1));
      memcpy(*r->service_config_json_out + service_config_len, result->txt,
             result->length);
      service_config_len += result->length;
    }
    (*r->service_config_json_out)[service_config_len] = '\0';
    GRPC_CARES_TRACE_LOG("request:%p found service config: %s", r,
                         *r->service_config_json_out);
  }
  // Clean up.
  ares_free_data(reply);
  return;
fail:
  std::string error_msg =
      absl::StrFormat("C-ares status is not ARES_SUCCESS qtype=TXT name=%s: %s",
                      q->name(), ares_strerror(status));
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked %s", r,
                       error_msg.c_str());
  error = GRPC_ERROR_CREATE_FROM_CPP_STRING(std::move(error_msg));
  r->error = grpc_error_add_child(error, r->error);
}

void grpc_dns_lookup_ares_continue_after_check_localhost_and_ip_literals_locked(
    grpc_ares_request* r, const char* dns_server, const char* name,
    const char* default_port, grpc_pollset_set* interested_parties,
    int query_timeout_ms) ABSL_EXCLUSIVE_LOCKS_REQUIRED(r->mu) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_ares_hostbyname_request* hr = nullptr;
  /* parse name, splitting it into host and port parts */
  std::string host;
  std::string port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    error = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS, name);
    goto error_cleanup;
  } else if (port.empty()) {
    if (default_port == nullptr || strlen(default_port) == 0) {
      error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS, name);
      goto error_cleanup;
    }
    port = default_port;
  }
  error = grpc_ares_ev_driver_create_locked(&r->ev_driver, interested_parties,
                                            query_timeout_ms, r);
  if (error != GRPC_ERROR_NONE) goto error_cleanup;
  // If dns_server is specified, use it.
  if (dns_server != nullptr && dns_server[0] != '\0') {
    GRPC_CARES_TRACE_LOG("request:%p Using DNS server %s", r, dns_server);
    grpc_resolved_address addr;
    if (grpc_parse_ipv4_hostport(dns_server, &addr, false /* log_errors */)) {
      r->dns_server_addr.family = AF_INET;
      struct sockaddr_in* in = reinterpret_cast<struct sockaddr_in*>(addr.addr);
      memcpy(&r->dns_server_addr.addr.addr4, &in->sin_addr,
             sizeof(struct in_addr));
      r->dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      r->dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else if (grpc_parse_ipv6_hostport(dns_server, &addr,
                                        false /* log_errors */)) {
      r->dns_server_addr.family = AF_INET6;
      struct sockaddr_in6* in6 =
          reinterpret_cast<struct sockaddr_in6*>(addr.addr);
      memcpy(&r->dns_server_addr.addr.addr6, &in6->sin6_addr,
             sizeof(struct in6_addr));
      r->dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      r->dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else {
      error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse authority"),
          GRPC_ERROR_STR_TARGET_ADDRESS, name);
      goto error_cleanup;
    }
    int status =
        ares_set_servers_ports(r->ev_driver->channel, &r->dns_server_addr);
    if (status != ARES_SUCCESS) {
      error = GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
          "C-ares status is not ARES_SUCCESS: ", ares_strerror(status)));
      goto error_cleanup;
    }
  }
  r->pending_queries = 1;
  if (grpc_ares_query_ipv6()) {
    hr = create_hostbyname_request_locked(r, host.c_str(),
                                          grpc_strhtons(port.c_str()),
                                          /*is_balancer=*/false, "AAAA");
    ares_gethostbyname(r->ev_driver->channel, hr->host, AF_INET6,
                       on_hostbyname_done_locked, hr);
  }
  hr = create_hostbyname_request_locked(r, host.c_str(),
                                        grpc_strhtons(port.c_str()),
                                        /*is_balancer=*/false, "A");
  ares_gethostbyname(r->ev_driver->channel, hr->host, AF_INET,
                     on_hostbyname_done_locked, hr);
  if (r->balancer_addresses_out != nullptr) {
    /* Query the SRV record */
    std::string service_name = absl::StrCat("_grpclb._tcp.", host);
    GrpcAresQuery* srv_query = new GrpcAresQuery(r, service_name);
    ares_query(r->ev_driver->channel, service_name.c_str(), ns_c_in, ns_t_srv,
               on_srv_query_done_locked, srv_query);
  }
  if (r->service_config_json_out != nullptr) {
    std::string config_name = absl::StrCat("_grpc_config.", host);
    GrpcAresQuery* txt_query = new GrpcAresQuery(r, config_name);
    ares_search(r->ev_driver->channel, config_name.c_str(), ns_c_in, ns_t_txt,
                on_txt_done_locked, txt_query);
  }
  grpc_ares_ev_driver_start_locked(r->ev_driver);
  grpc_ares_request_unref_locked(r);
  return;

error_cleanup:
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done, error);
}

static bool inner_resolve_as_ip_literal_locked(
    const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs, std::string* host,
    std::string* port, std::string* hostport) {
  if (!grpc_core::SplitHostPort(name, host, port)) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s to host:port while attempting to resolve as ip "
            "literal.",
            name);
    return false;
  }
  if (port->empty()) {
    if (default_port == nullptr || strlen(default_port) == 0) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s while attempting to resolve as "
              "ip literal.",
              name);
      return false;
    }
    *port = default_port;
  }
  grpc_resolved_address addr;
  *hostport = grpc_core::JoinHostPort(*host, atoi(port->c_str()));
  if (grpc_parse_ipv4_hostport(hostport->c_str(), &addr,
                               false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport->c_str(), &addr,
                               false /* log errors */)) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = absl::make_unique<ServerAddressList>();
    (*addrs)->emplace_back(addr.addr, addr.len, nullptr /* args */);
    return true;
  }
  return false;
}

static bool resolve_as_ip_literal_locked(
    const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs) {
  std::string host;
  std::string port;
  std::string hostport;
  bool out = inner_resolve_as_ip_literal_locked(name, default_port, addrs,
                                                &host, &port, &hostport);
  return out;
}

static bool target_matches_localhost_inner(const char* name, std::string* host,
                                           std::string* port) {
  if (!grpc_core::SplitHostPort(name, host, port)) {
    gpr_log(GPR_ERROR, "Unable to split host and port for name: %s", name);
    return false;
  }
  return gpr_stricmp(host->c_str(), "localhost") == 0;
}

static bool target_matches_localhost(const char* name) {
  std::string host;
  std::string port;
  return target_matches_localhost_inner(name, &host, &port);
}

#ifdef GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY
static bool inner_maybe_resolve_localhost_manually_locked(
    const grpc_ares_request* r, const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs, std::string* host,
    std::string* port) {
  grpc_core::SplitHostPort(name, host, port);
  if (host->empty()) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s into host:port during manual localhost "
            "resolution check.",
            name);
    return false;
  }
  if (port->empty()) {
    if (default_port == nullptr || strlen(default_port) == 0) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s during manual localhost "
              "resolution check.",
              name);
      return false;
    }
    *port = default_port;
  }
  if (gpr_stricmp(host->c_str(), "localhost") == 0) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = absl::make_unique<grpc_core::ServerAddressList>();
    uint16_t numeric_port = grpc_strhtons(port->c_str());
    // Append the ipv6 loopback address.
    struct sockaddr_in6 ipv6_loopback_addr;
    memset(&ipv6_loopback_addr, 0, sizeof(ipv6_loopback_addr));
    ((char*)&ipv6_loopback_addr.sin6_addr)[15] = 1;
    ipv6_loopback_addr.sin6_family = AF_INET6;
    ipv6_loopback_addr.sin6_port = numeric_port;
    (*addrs)->emplace_back(&ipv6_loopback_addr, sizeof(ipv6_loopback_addr),
                           nullptr /* args */);
    // Append the ipv4 loopback address.
    struct sockaddr_in ipv4_loopback_addr;
    memset(&ipv4_loopback_addr, 0, sizeof(ipv4_loopback_addr));
    ((char*)&ipv4_loopback_addr.sin_addr)[0] = 0x7f;
    ((char*)&ipv4_loopback_addr.sin_addr)[3] = 0x01;
    ipv4_loopback_addr.sin_family = AF_INET;
    ipv4_loopback_addr.sin_port = numeric_port;
    (*addrs)->emplace_back(&ipv4_loopback_addr, sizeof(ipv4_loopback_addr),
                           nullptr /* args */);
    // Let the address sorter figure out which one should be tried first.
    grpc_cares_wrapper_address_sorting_sort(r, addrs->get());
    return true;
  }
  return false;
}

static bool grpc_ares_maybe_resolve_localhost_manually_locked(
    const grpc_ares_request* r, const char* name, const char* default_port,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs) {
  std::string host;
  std::string port;
  return inner_maybe_resolve_localhost_manually_locked(r, name, default_port,
                                                       addrs, &host, &port);
}
#else  /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */
static bool grpc_ares_maybe_resolve_localhost_manually_locked(
    const grpc_ares_request* /*r*/, const char* /*name*/,
    const char* /*default_port*/,
    std::unique_ptr<grpc_core::ServerAddressList>* /*addrs*/) {
  return false;
}
#endif /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */

static grpc_ares_request* grpc_dns_lookup_ares_impl(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addrs,
    char** service_config_json, int query_timeout_ms) {
  grpc_ares_request* r = new grpc_ares_request();
  grpc_core::MutexLock lock(&r->mu);
  r->ev_driver = nullptr;
  r->on_done = on_done;
  r->addresses_out = addrs;
  r->balancer_addresses_out = balancer_addrs;
  r->service_config_json_out = service_config_json;
  GRPC_CARES_TRACE_LOG(
      "request:%p c-ares grpc_dns_lookup_ares_impl name=%s, "
      "default_port=%s",
      r, name, default_port);
  // Early out if the target is an ipv4 or ipv6 literal.
  if (resolve_as_ip_literal_locked(name, default_port, addrs)) {
    grpc_ares_complete_request_locked(r);
    return r;
  }
  // Early out if the target is localhost and we're on Windows.
  if (grpc_ares_maybe_resolve_localhost_manually_locked(r, name, default_port,
                                                        addrs)) {
    grpc_ares_complete_request_locked(r);
    return r;
  }
  // Don't query for SRV and TXT records if the target is "localhost", so
  // as to cut down on lookups over the network, especially in tests:
  // https://github.com/grpc/proposal/pull/79
  if (target_matches_localhost(name)) {
    r->balancer_addresses_out = nullptr;
    r->service_config_json_out = nullptr;
  }
  // Look up name using c-ares lib.
  grpc_dns_lookup_ares_continue_after_check_localhost_and_ip_literals_locked(
      r, dns_server, name, default_port, interested_parties, query_timeout_ms);
  return r;
}

grpc_ares_request* (*grpc_dns_lookup_ares)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addrs,
    char** service_config_json,
    int query_timeout_ms) = grpc_dns_lookup_ares_impl;

static void grpc_cancel_ares_request_impl(grpc_ares_request* r) {
  GPR_ASSERT(r != nullptr);
  grpc_core::MutexLock lock(&r->mu);
  GRPC_CARES_TRACE_LOG("request:%p grpc_cancel_ares_request ev_driver:%p", r,
                       r->ev_driver);
  if (r->ev_driver != nullptr) {
    grpc_ares_ev_driver_shutdown_locked(r->ev_driver);
  }
}

void (*grpc_cancel_ares_request)(grpc_ares_request* r) =
    grpc_cancel_ares_request_impl;

// ares_library_init and ares_library_cleanup are currently no-op except under
// Windows. Calling them may cause race conditions when other parts of the
// binary calls these functions concurrently.
#ifdef GPR_WINDOWS
grpc_error_handle grpc_ares_init(void) {
  int status = ares_library_init(ARES_LIB_INIT_ALL);
  if (status != ARES_SUCCESS) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("ares_library_init failed: ", ares_strerror(status)));
  }
  return GRPC_ERROR_NONE;
}

void grpc_ares_cleanup(void) { ares_library_cleanup(); }
#else
grpc_error_handle grpc_ares_init(void) { return GRPC_ERROR_NONE; }
void grpc_ares_cleanup(void) {}
#endif  // GPR_WINDOWS

#endif /* GRPC_ARES == 1 */
