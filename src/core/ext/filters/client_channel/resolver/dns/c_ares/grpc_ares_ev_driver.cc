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

#include "src/core/lib/iomgr/port.h"
#if GRPC_ARES == 1 && !defined(GRPC_UV)

#include <ares.h>
#include <string.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"

typedef struct fd_node {
  /** the owner of this fd node */
  grpc_ares_ev_driver* ev_driver;
  /** a closure wrapping on_readable_locked, which should be
     invoked when the grpc_fd in this node becomes readable. */
  grpc_closure read_closure;
  /** a closure wrapping on_writable_locked, which should be
     invoked when the grpc_fd in this node becomes writable. */
  grpc_closure write_closure;
  /** next fd node in the list */
  struct fd_node* next;

  /** wrapped fd that's polled by grpc's poller for the current platform */
  grpc_core::GrpcPolledFd* grpc_polled_fd;
  /** if the readable closure has been registered */
  bool readable_registered;
  /** if the writable closure has been registered */
  bool writable_registered;
  /** if the fd has been shutdown yet from grpc iomgr perspective */
  bool already_shutdown;
} fd_node;

struct grpc_ares_ev_driver {
  /** the ares_channel owned by this event driver */
  ares_channel channel;
  /** pollset set for driving the IO events of the channel */
  grpc_pollset_set* pollset_set;
  /** refcount of the event driver */
  gpr_refcount refs;

  /** combiner to synchronize c-ares and I/O callbacks on */
  grpc_combiner* combiner;
  /** a list of grpc_fd that this event driver is currently using. */
  fd_node* fds;
  /** is this event driver currently working? */
  bool working;
  /** is this event driver being shut down */
  bool shutting_down;
  /** request object that's using this ev driver */
  grpc_ares_request* request;
  /** Owned by the ev_driver. Creates new GrpcPolledFd's */
  grpc_core::UniquePtr<grpc_core::GrpcPolledFdFactory> polled_fd_factory;
  /** query timeout in milliseconds */
  int query_timeout_ms;
  /** alarm to cancel active queries */
  grpc_timer query_timeout;
  /** cancels queries on a timeout */
  grpc_closure on_timeout_locked;
};

static void grpc_ares_notify_on_event_locked(grpc_ares_ev_driver* ev_driver);

static grpc_ares_ev_driver* grpc_ares_ev_driver_ref(
    grpc_ares_ev_driver* ev_driver) {
  GRPC_CARES_TRACE_LOG("request:%p Ref ev_driver %p", ev_driver->request,
                       ev_driver);
  gpr_ref(&ev_driver->refs);
  return ev_driver;
}

static void grpc_ares_ev_driver_unref(grpc_ares_ev_driver* ev_driver) {
  GRPC_CARES_TRACE_LOG("request:%p Unref ev_driver %p", ev_driver->request,
                       ev_driver);
  if (gpr_unref(&ev_driver->refs)) {
    GRPC_CARES_TRACE_LOG("request:%p destroy ev_driver %p", ev_driver->request,
                         ev_driver);
    GPR_ASSERT(ev_driver->fds == nullptr);
    GRPC_COMBINER_UNREF(ev_driver->combiner, "free ares event driver");
    ares_destroy(ev_driver->channel);
    grpc_ares_complete_request_locked(ev_driver->request);
    grpc_core::Delete(ev_driver);
  }
}

static void fd_node_destroy_locked(fd_node* fdn) {
  GRPC_CARES_TRACE_LOG("request:%p delete fd: %s", fdn->ev_driver->request,
                       fdn->grpc_polled_fd->GetName());
  GPR_ASSERT(!fdn->readable_registered);
  GPR_ASSERT(!fdn->writable_registered);
  GPR_ASSERT(fdn->already_shutdown);
  grpc_core::Delete(fdn->grpc_polled_fd);
  gpr_free(fdn);
}

static void fd_node_shutdown_locked(fd_node* fdn, const char* reason) {
  if (!fdn->already_shutdown) {
    fdn->already_shutdown = true;
    fdn->grpc_polled_fd->ShutdownLocked(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(reason));
  }
}

static void on_timeout_locked(void* arg, grpc_error* error);

grpc_error* grpc_ares_ev_driver_create_locked(grpc_ares_ev_driver** ev_driver,
                                              grpc_pollset_set* pollset_set,
                                              int query_timeout_ms,
                                              grpc_combiner* combiner,
                                              grpc_ares_request* request) {
  *ev_driver = grpc_core::New<grpc_ares_ev_driver>();
  ares_options opts;
  memset(&opts, 0, sizeof(opts));
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&(*ev_driver)->channel, &opts, ARES_OPT_FLAGS);
  GRPC_CARES_TRACE_LOG("request:%p grpc_ares_ev_driver_create_locked", request);
  if (status != ARES_SUCCESS) {
    char* err_msg;
    gpr_asprintf(&err_msg, "Failed to init ares channel. C-ares error: %s",
                 ares_strerror(status));
    grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_msg);
    gpr_free(err_msg);
    gpr_free(*ev_driver);
    return err;
  }
  (*ev_driver)->combiner = GRPC_COMBINER_REF(combiner, "ares event driver");
  gpr_ref_init(&(*ev_driver)->refs, 1);
  (*ev_driver)->pollset_set = pollset_set;
  (*ev_driver)->fds = nullptr;
  (*ev_driver)->working = false;
  (*ev_driver)->shutting_down = false;
  (*ev_driver)->request = request;
  (*ev_driver)->polled_fd_factory =
      grpc_core::NewGrpcPolledFdFactory((*ev_driver)->combiner);
  (*ev_driver)
      ->polled_fd_factory->ConfigureAresChannelLocked((*ev_driver)->channel);
  GRPC_CLOSURE_INIT(&(*ev_driver)->on_timeout_locked, on_timeout_locked,
                    *ev_driver, grpc_combiner_scheduler(combiner));
  (*ev_driver)->query_timeout_ms = query_timeout_ms;
  return GRPC_ERROR_NONE;
}

void grpc_ares_ev_driver_on_queries_complete_locked(
    grpc_ares_ev_driver* ev_driver) {
  // We mark the event driver as being shut down. If the event driver
  // is working, grpc_ares_notify_on_event_locked will shut down the
  // fds; if it's not working, there are no fds to shut down.
  ev_driver->shutting_down = true;
  grpc_timer_cancel(&ev_driver->query_timeout);
  grpc_ares_ev_driver_unref(ev_driver);
}

void grpc_ares_ev_driver_shutdown_locked(grpc_ares_ev_driver* ev_driver) {
  ev_driver->shutting_down = true;
  fd_node* fn = ev_driver->fds;
  while (fn != nullptr) {
    fd_node_shutdown_locked(fn, "grpc_ares_ev_driver_shutdown");
    fn = fn->next;
  }
}

// Search fd in the fd_node list head. This is an O(n) search, the max possible
// value of n is ARES_GETSOCK_MAXNUM (16). n is typically 1 - 2 in our tests.
static fd_node* pop_fd_node_locked(fd_node** head, ares_socket_t as) {
  fd_node dummy_head;
  dummy_head.next = *head;
  fd_node* node = &dummy_head;
  while (node->next != nullptr) {
    if (node->next->grpc_polled_fd->GetWrappedAresSocketLocked() == as) {
      fd_node* ret = node->next;
      node->next = node->next->next;
      *head = dummy_head.next;
      return ret;
    }
    node = node->next;
  }
  return nullptr;
}

static void on_timeout_locked(void* arg, grpc_error* error) {
  grpc_ares_ev_driver* driver = static_cast<grpc_ares_ev_driver*>(arg);
  GRPC_CARES_TRACE_LOG(
      "request:%p ev_driver=%p on_timeout_locked. driver->shutting_down=%d. "
      "err=%s",
      driver->request, driver, driver->shutting_down, grpc_error_string(error));
  if (!driver->shutting_down && error == GRPC_ERROR_NONE) {
    grpc_ares_ev_driver_shutdown_locked(driver);
  }
  grpc_ares_ev_driver_unref(driver);
}

static void on_readable_locked(void* arg, grpc_error* error) {
  fd_node* fdn = static_cast<fd_node*>(arg);
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

static void on_writable_locked(void* arg, grpc_error* error) {
  fd_node* fdn = static_cast<fd_node*>(arg);
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

ares_channel* grpc_ares_ev_driver_get_channel_locked(
    grpc_ares_ev_driver* ev_driver) {
  return &ev_driver->channel;
}

// Get the file descriptors used by the ev_driver's ares channel, register
// driver_closure with these filedescriptors.
static void grpc_ares_notify_on_event_locked(grpc_ares_ev_driver* ev_driver) {
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
          fdn = static_cast<fd_node*>(gpr_malloc(sizeof(fd_node)));
          fdn->grpc_polled_fd =
              ev_driver->polled_fd_factory->NewGrpcPolledFdLocked(
                  socks[i], ev_driver->pollset_set, ev_driver->combiner);
          GRPC_CARES_TRACE_LOG("request:%p new fd: %s", ev_driver->request,
                               fdn->grpc_polled_fd->GetName());
          fdn->ev_driver = ev_driver;
          fdn->readable_registered = false;
          fdn->writable_registered = false;
          fdn->already_shutdown = false;
          GRPC_CLOSURE_INIT(&fdn->read_closure, on_readable_locked, fdn,
                            grpc_combiner_scheduler(ev_driver->combiner));
          GRPC_CLOSURE_INIT(&fdn->write_closure, on_writable_locked, fdn,
                            grpc_combiner_scheduler(ev_driver->combiner));
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
  // If the ev driver has no working fd, all the tasks are done.
  if (new_list == nullptr) {
    ev_driver->working = false;
    GRPC_CARES_TRACE_LOG("request:%p ev driver stop working",
                         ev_driver->request);
  }
}

void grpc_ares_ev_driver_start_locked(grpc_ares_ev_driver* ev_driver) {
  if (!ev_driver->working) {
    ev_driver->working = true;
    grpc_ares_notify_on_event_locked(ev_driver);
    grpc_millis timeout =
        ev_driver->query_timeout_ms == 0
            ? GRPC_MILLIS_INF_FUTURE
            : ev_driver->query_timeout_ms + grpc_core::ExecCtx::Get()->Now();
    GRPC_CARES_TRACE_LOG(
        "request:%p ev_driver=%p grpc_ares_ev_driver_start_locked. timeout in "
        "%" PRId64 " ms",
        ev_driver->request, ev_driver, timeout);
    grpc_ares_ev_driver_ref(ev_driver);
    grpc_timer_init(&ev_driver->query_timeout, timeout,
                    &ev_driver->on_timeout_locked);
  }
}

#endif /* GRPC_ARES == 1 && !defined(GRPC_UV) */
