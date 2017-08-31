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
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET)

#include <ares.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/string.h"

typedef struct fd_node {
  /** the owner of this fd node */
  grpc_ares_ev_driver *ev_driver;
  /** the grpc_fd owned by this fd node */
  grpc_fd *grpc_fd;
  /** a closure wrapping on_readable_cb, which should be invoked when the
      grpc_fd in this node becomes readable. */
  grpc_closure read_closure;
  /** a closure wrapping on_writable_cb, which should be invoked when the
      grpc_fd in this node becomes writable. */
  grpc_closure write_closure;
  /** next fd node in the list */
  struct fd_node *next;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** if the readable closure has been registered */
  bool readable_registered;
  /** if the writable closure has been registered */
  bool writable_registered;
} fd_node;

struct grpc_ares_ev_driver {
  /** the ares_channel owned by this event driver */
  ares_channel channel;
  /** pollset set for driving the IO events of the channel */
  grpc_pollset_set *pollset_set;
  /** refcount of the event driver */
  gpr_refcount refs;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** a list of grpc_fd that this event driver is currently using. */
  fd_node *fds;
  /** is this event driver currently working? */
  bool working;
  /** is this event driver being shut down */
  bool shutting_down;
};

static void grpc_ares_notify_on_event_locked(grpc_exec_ctx *exec_ctx,
                                             grpc_ares_ev_driver *ev_driver);

static grpc_ares_ev_driver *grpc_ares_ev_driver_ref(
    grpc_ares_ev_driver *ev_driver) {
  gpr_log(GPR_DEBUG, "Ref ev_driver %" PRIuPTR, (uintptr_t)ev_driver);
  gpr_ref(&ev_driver->refs);
  return ev_driver;
}

static void grpc_ares_ev_driver_unref(grpc_ares_ev_driver *ev_driver) {
  gpr_log(GPR_DEBUG, "Unref ev_driver %" PRIuPTR, (uintptr_t)ev_driver);
  if (gpr_unref(&ev_driver->refs)) {
    gpr_log(GPR_DEBUG, "destroy ev_driver %" PRIuPTR, (uintptr_t)ev_driver);
    GPR_ASSERT(ev_driver->fds == NULL);
    gpr_mu_destroy(&ev_driver->mu);
    ares_destroy(ev_driver->channel);
    gpr_free(ev_driver);
  }
}

static void fd_node_destroy(grpc_exec_ctx *exec_ctx, fd_node *fdn) {
  gpr_log(GPR_DEBUG, "delete fd: %d", grpc_fd_wrapped_fd(fdn->grpc_fd));
  GPR_ASSERT(!fdn->readable_registered);
  GPR_ASSERT(!fdn->writable_registered);
  gpr_mu_destroy(&fdn->mu);
  grpc_pollset_set_del_fd(exec_ctx, fdn->ev_driver->pollset_set, fdn->grpc_fd);
  /* c-ares library has closed the fd inside grpc_fd. This fd may be picked up
     immediately by another thread, and should not be closed by the following
     grpc_fd_orphan. */
  grpc_fd_orphan(exec_ctx, fdn->grpc_fd, NULL, NULL, true /* already_closed */,
                 "c-ares query finished");
  gpr_free(fdn);
}

grpc_error *grpc_ares_ev_driver_create(grpc_ares_ev_driver **ev_driver,
                                       grpc_pollset_set *pollset_set) {
  *ev_driver = gpr_malloc(sizeof(grpc_ares_ev_driver));
  int status = ares_init(&(*ev_driver)->channel);
  gpr_log(GPR_DEBUG, "grpc_ares_ev_driver_create");
  if (status != ARES_SUCCESS) {
    char *err_msg;
    gpr_asprintf(&err_msg, "Failed to init ares channel. C-ares error: %s",
                 ares_strerror(status));
    grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_msg);
    gpr_free(err_msg);
    gpr_free(*ev_driver);
    return err;
  }
  gpr_mu_init(&(*ev_driver)->mu);
  gpr_ref_init(&(*ev_driver)->refs, 1);
  (*ev_driver)->pollset_set = pollset_set;
  (*ev_driver)->fds = NULL;
  (*ev_driver)->working = false;
  (*ev_driver)->shutting_down = false;
  return GRPC_ERROR_NONE;
}

void grpc_ares_ev_driver_destroy(grpc_ares_ev_driver *ev_driver) {
  // It's not safe to shut down remaining fds here directly, becauses
  // ares_host_callback does not provide an exec_ctx. We mark the event driver
  // as being shut down. If the event driver is working,
  // grpc_ares_notify_on_event_locked will shut down the fds; if it's not
  // working, there are no fds to shut down.
  gpr_mu_lock(&ev_driver->mu);
  ev_driver->shutting_down = true;
  gpr_mu_unlock(&ev_driver->mu);
  grpc_ares_ev_driver_unref(ev_driver);
}

void grpc_ares_ev_driver_shutdown(grpc_exec_ctx *exec_ctx,
                                  grpc_ares_ev_driver *ev_driver) {
  gpr_mu_lock(&ev_driver->mu);
  ev_driver->shutting_down = true;
  fd_node *fn = ev_driver->fds;
  while (fn != NULL) {
    grpc_fd_shutdown(
        exec_ctx, fn->grpc_fd,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("grpc_ares_ev_driver_shutdown"));
    fn = fn->next;
  }
  gpr_mu_unlock(&ev_driver->mu);
}

// Search fd in the fd_node list head. This is an O(n) search, the max possible
// value of n is ARES_GETSOCK_MAXNUM (16). n is typically 1 - 2 in our tests.
static fd_node *pop_fd_node(fd_node **head, int fd) {
  fd_node dummy_head;
  dummy_head.next = *head;
  fd_node *node = &dummy_head;
  while (node->next != NULL) {
    if (grpc_fd_wrapped_fd(node->next->grpc_fd) == fd) {
      fd_node *ret = node->next;
      node->next = node->next->next;
      *head = dummy_head.next;
      return ret;
    }
    node = node->next;
  }
  return NULL;
}

static void on_readable_cb(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  fd_node *fdn = arg;
  grpc_ares_ev_driver *ev_driver = fdn->ev_driver;
  gpr_mu_lock(&fdn->mu);
  fdn->readable_registered = false;
  gpr_mu_unlock(&fdn->mu);

  gpr_log(GPR_DEBUG, "readable on %d", grpc_fd_wrapped_fd(fdn->grpc_fd));
  if (error == GRPC_ERROR_NONE) {
    ares_process_fd(ev_driver->channel, grpc_fd_wrapped_fd(fdn->grpc_fd),
                    ARES_SOCKET_BAD);
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this ev_driver will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be invoked
    // with a status of ARES_ECANCELLED. The remaining file descriptors in this
    // ev_driver will be cleaned up in the follwing
    // grpc_ares_notify_on_event_locked().
    ares_cancel(ev_driver->channel);
  }
  gpr_mu_lock(&ev_driver->mu);
  grpc_ares_notify_on_event_locked(exec_ctx, ev_driver);
  gpr_mu_unlock(&ev_driver->mu);
  grpc_ares_ev_driver_unref(ev_driver);
}

static void on_writable_cb(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  fd_node *fdn = arg;
  grpc_ares_ev_driver *ev_driver = fdn->ev_driver;
  gpr_mu_lock(&fdn->mu);
  fdn->writable_registered = false;
  gpr_mu_unlock(&fdn->mu);

  gpr_log(GPR_DEBUG, "writable on %d", grpc_fd_wrapped_fd(fdn->grpc_fd));
  if (error == GRPC_ERROR_NONE) {
    ares_process_fd(ev_driver->channel, ARES_SOCKET_BAD,
                    grpc_fd_wrapped_fd(fdn->grpc_fd));
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this ev_driver will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be invoked
    // with a status of ARES_ECANCELLED. The remaining file descriptors in this
    // ev_driver will be cleaned up in the follwing
    // grpc_ares_notify_on_event_locked().
    ares_cancel(ev_driver->channel);
  }
  gpr_mu_lock(&ev_driver->mu);
  grpc_ares_notify_on_event_locked(exec_ctx, ev_driver);
  gpr_mu_unlock(&ev_driver->mu);
  grpc_ares_ev_driver_unref(ev_driver);
}

ares_channel *grpc_ares_ev_driver_get_channel(grpc_ares_ev_driver *ev_driver) {
  return &ev_driver->channel;
}

// Get the file descriptors used by the ev_driver's ares channel, register
// driver_closure with these filedescriptors.
static void grpc_ares_notify_on_event_locked(grpc_exec_ctx *exec_ctx,
                                             grpc_ares_ev_driver *ev_driver) {
  fd_node *new_list = NULL;
  if (!ev_driver->shutting_down) {
    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int socks_bitmask =
        ares_getsock(ev_driver->channel, socks, ARES_GETSOCK_MAXNUM);
    for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
          ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
        fd_node *fdn = pop_fd_node(&ev_driver->fds, socks[i]);
        // Create a new fd_node if sock[i] is not in the fd_node list.
        if (fdn == NULL) {
          char *fd_name;
          gpr_asprintf(&fd_name, "ares_ev_driver-%" PRIuPTR, i);
          fdn = gpr_malloc(sizeof(fd_node));
          gpr_log(GPR_DEBUG, "new fd: %d", socks[i]);
          fdn->grpc_fd = grpc_fd_create(socks[i], fd_name);
          fdn->ev_driver = ev_driver;
          fdn->readable_registered = false;
          fdn->writable_registered = false;
          gpr_mu_init(&fdn->mu);
          GRPC_CLOSURE_INIT(&fdn->read_closure, on_readable_cb, fdn,
                            grpc_schedule_on_exec_ctx);
          GRPC_CLOSURE_INIT(&fdn->write_closure, on_writable_cb, fdn,
                            grpc_schedule_on_exec_ctx);
          grpc_pollset_set_add_fd(exec_ctx, ev_driver->pollset_set,
                                  fdn->grpc_fd);
          gpr_free(fd_name);
        }
        fdn->next = new_list;
        new_list = fdn;
        gpr_mu_lock(&fdn->mu);
        // Register read_closure if the socket is readable and read_closure has
        // not been registered with this socket.
        if (ARES_GETSOCK_READABLE(socks_bitmask, i) &&
            !fdn->readable_registered) {
          grpc_ares_ev_driver_ref(ev_driver);
          gpr_log(GPR_DEBUG, "notify read on: %d",
                  grpc_fd_wrapped_fd(fdn->grpc_fd));
          grpc_fd_notify_on_read(exec_ctx, fdn->grpc_fd, &fdn->read_closure);
          fdn->readable_registered = true;
        }
        // Register write_closure if the socket is writable and write_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_WRITABLE(socks_bitmask, i) &&
            !fdn->writable_registered) {
          gpr_log(GPR_DEBUG, "notify write on: %d",
                  grpc_fd_wrapped_fd(fdn->grpc_fd));
          grpc_ares_ev_driver_ref(ev_driver);
          grpc_fd_notify_on_write(exec_ctx, fdn->grpc_fd, &fdn->write_closure);
          fdn->writable_registered = true;
        }
        gpr_mu_unlock(&fdn->mu);
      }
    }
  }
  // Any remaining fds in ev_driver->fds were not returned by ares_getsock() and
  // are therefore no longer in use, so they can be shut down and removed from
  // the list.
  while (ev_driver->fds != NULL) {
    fd_node *cur = ev_driver->fds;
    ev_driver->fds = ev_driver->fds->next;
    fd_node_destroy(exec_ctx, cur);
  }
  ev_driver->fds = new_list;
  // If the ev driver has no working fd, all the tasks are done.
  if (new_list == NULL) {
    ev_driver->working = false;
    gpr_log(GPR_DEBUG, "ev driver stop working");
  }
}

void grpc_ares_ev_driver_start(grpc_exec_ctx *exec_ctx,
                               grpc_ares_ev_driver *ev_driver) {
  gpr_mu_lock(&ev_driver->mu);
  if (!ev_driver->working) {
    ev_driver->working = true;
    grpc_ares_notify_on_event_locked(exec_ctx, ev_driver);
  }
  gpr_mu_unlock(&ev_driver->mu);
}

#endif /* GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET) */
