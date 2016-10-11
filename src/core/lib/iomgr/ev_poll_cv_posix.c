/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/lib/iomgr/ev_poll_cv_posix.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_poll_posix.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

#define POLL_PERIOD_MS 1000
#define DEFAULT_TABLE_SIZE 16

typedef enum status_t { INPROGRESS, COMPLETED, CANCELLED } status_t;

typedef struct poll_args {
  gpr_refcount refcount;
  gpr_cv* cv;
  struct pollfd* fds;
  nfds_t nfds;
  int timeout;
  int retval;
  int err;
  status_t status;
} poll_args;

cv_fd_table g_cvfds;

static void decref_poll_args(poll_args* args) {
  if (gpr_unref(&args->refcount)) {
    gpr_free(args->fds);
    gpr_cv_destroy(args->cv);
    gpr_free(args->cv);
    gpr_free(args);
  }
}

// Poll in a background thread
static void run_poll(void* arg) {
  int timeout, retval;
  poll_args* pargs = (poll_args*)arg;
  while (pargs->status == INPROGRESS) {
    if (pargs->timeout < 0) {
      timeout = POLL_PERIOD_MS;
    } else {
      timeout = GPR_MIN(POLL_PERIOD_MS, pargs->timeout);
      pargs->timeout -= timeout;
    }
    retval = g_cvfds.poll(pargs->fds, pargs->nfds, timeout);
    if (retval != 0 || pargs->timeout == 0) {
      pargs->retval = retval;
      pargs->err = errno;
      break;
    }
  }
  gpr_mu_lock(&g_cvfds.mu);
  if (pargs->status == INPROGRESS) {
    // Signal main thread that the poll completed
    pargs->status = COMPLETED;
    gpr_cv_signal(pargs->cv);
  }
  decref_poll_args(pargs);
  g_cvfds.pollcount--;
  if (g_cvfds.shutdown && g_cvfds.pollcount == 0) {
    gpr_cv_signal(&g_cvfds.shutdown_complete);
  }
  gpr_mu_unlock(&g_cvfds.mu);
}

// This function overrides poll() to handle condition variable wakeup fds
static int cvfd_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
  unsigned int i;
  int res, idx;
  gpr_cv* pollcv;
  cv_node *cvn, *prev;
  nfds_t nsockfds = 0;
  gpr_thd_id t_id;
  gpr_thd_options opt;
  poll_args* pargs = NULL;
  gpr_mu_lock(&g_cvfds.mu);
  pollcv = gpr_malloc(sizeof(gpr_cv));
  gpr_cv_init(pollcv);
  for (i = 0; i < nfds; i++) {
    fds[i].revents = 0;
    if (fds[i].fd < 0 && (fds[i].events & POLLIN)) {
      idx = FD_TO_IDX(fds[i].fd);
      cvn = gpr_malloc(sizeof(cv_node));
      cvn->cv = pollcv;
      cvn->next = g_cvfds.cvfds[idx].cvs;
      g_cvfds.cvfds[idx].cvs = cvn;
      // We should return immediately if there are pending events,
      // but we still need to call poll() to check for socket events
      if (g_cvfds.cvfds[idx].is_set) {
        timeout = 0;
      }
    } else if (fds[i].fd >= 0) {
      nsockfds++;
    }
  }

  if (nsockfds > 0) {
    pargs = gpr_malloc(sizeof(struct poll_args));
    // Both the main thread and calling thread get a reference
    gpr_ref_init(&pargs->refcount, 2);
    pargs->cv = pollcv;
    pargs->fds = gpr_malloc(sizeof(struct pollfd) * nsockfds);
    pargs->nfds = nsockfds;
    pargs->timeout = timeout;
    pargs->retval = 0;
    pargs->err = 0;
    pargs->status = INPROGRESS;
    idx = 0;
    for (i = 0; i < nfds; i++) {
      if (fds[i].fd >= 0) {
        pargs->fds[idx].fd = fds[i].fd;
        pargs->fds[idx].events = fds[i].events;
        pargs->fds[idx].revents = 0;
        idx++;
      }
    }
    g_cvfds.pollcount++;
    opt = gpr_thd_options_default();
    gpr_thd_options_set_detached(&opt);
    gpr_thd_new(&t_id, &run_poll, pargs, &opt);
    // We want the poll() thread to trigger the deadline, so wait forever here
    gpr_cv_wait(pollcv, &g_cvfds.mu, gpr_inf_future(GPR_CLOCK_MONOTONIC));
    if (pargs->status == COMPLETED) {
      res = pargs->retval;
      errno = pargs->err;
    } else {
      res = 0;
      errno = 0;
      pargs->status = CANCELLED;
    }
  } else {
    gpr_timespec deadline = gpr_now(GPR_CLOCK_REALTIME);
    deadline =
        gpr_time_add(deadline, gpr_time_from_millis(timeout, GPR_TIMESPAN));
    gpr_cv_wait(pollcv, &g_cvfds.mu, deadline);
    res = 0;
  }

  idx = 0;
  for (i = 0; i < nfds; i++) {
    if (fds[i].fd < 0 && (fds[i].events & POLLIN)) {
      cvn = g_cvfds.cvfds[FD_TO_IDX(fds[i].fd)].cvs;
      prev = NULL;
      while (cvn->cv != pollcv) {
        prev = cvn;
        cvn = cvn->next;
        GPR_ASSERT(cvn);
      }
      if (!prev) {
        g_cvfds.cvfds[FD_TO_IDX(fds[i].fd)].cvs = cvn->next;
      } else {
        prev->next = cvn->next;
      }
      gpr_free(cvn);

      if (g_cvfds.cvfds[FD_TO_IDX(fds[i].fd)].is_set) {
        fds[i].revents = POLLIN;
        if (res >= 0) res++;
      }
    } else if (fds[i].fd >= 0 && pargs->status == COMPLETED) {
      fds[i].revents = pargs->fds[idx].revents;
      idx++;
    }
  }

  if (pargs) {
    decref_poll_args(pargs);
  } else {
    gpr_cv_destroy(pollcv);
    gpr_free(pollcv);
  }
  gpr_mu_unlock(&g_cvfds.mu);

  return res;
}

static void grpc_global_cv_fd_table_init() {
  gpr_mu_init(&g_cvfds.mu);
  gpr_mu_lock(&g_cvfds.mu);
  gpr_cv_init(&g_cvfds.shutdown_complete);
  g_cvfds.shutdown = 0;
  g_cvfds.pollcount = 0;
  g_cvfds.size = DEFAULT_TABLE_SIZE;
  g_cvfds.cvfds = gpr_malloc(sizeof(fd_node) * DEFAULT_TABLE_SIZE);
  g_cvfds.free_fds = NULL;
  for (int i = 0; i < DEFAULT_TABLE_SIZE; i++) {
    g_cvfds.cvfds[i].is_set = 0;
    g_cvfds.cvfds[i].cvs = NULL;
    g_cvfds.cvfds[i].next_free = g_cvfds.free_fds;
    g_cvfds.free_fds = &g_cvfds.cvfds[i];
  }
  // Override the poll function with one that supports cvfds
  g_cvfds.poll = grpc_poll_function;
  grpc_poll_function = &cvfd_poll;
  gpr_mu_unlock(&g_cvfds.mu);
}

static void grpc_global_cv_fd_table_shutdown() {
  gpr_mu_lock(&g_cvfds.mu);
  g_cvfds.shutdown = 1;
  // Attempt to wait for all abandoned poll() threads to terminate
  // Not doing so will result in reported memory leaks
  if (g_cvfds.pollcount > 0) {
    int res = gpr_cv_wait(&g_cvfds.shutdown_complete, &g_cvfds.mu,
                          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                       gpr_time_from_seconds(3, GPR_TIMESPAN)));
    GPR_ASSERT(res == 0);
  }
  gpr_cv_destroy(&g_cvfds.shutdown_complete);
  grpc_poll_function = g_cvfds.poll;
  gpr_free(g_cvfds.cvfds);
  gpr_mu_unlock(&g_cvfds.mu);
  gpr_mu_destroy(&g_cvfds.mu);
}

/*******************************************************************************
 * event engine binding
 */

static const grpc_event_engine_vtable* ev_poll_vtable;
static grpc_event_engine_vtable vtable;

static void shutdown_engine(void) {
  ev_poll_vtable->shutdown_engine();
  grpc_global_cv_fd_table_shutdown();
}

const grpc_event_engine_vtable* grpc_init_poll_cv_posix(void) {
  grpc_global_cv_fd_table_init();
  grpc_enable_cv_wakeup_fds(1);
  ev_poll_vtable = grpc_init_poll_posix();
  if (!ev_poll_vtable) {
    grpc_global_cv_fd_table_shutdown();
    grpc_enable_cv_wakeup_fds(0);
    return NULL;
  }
  vtable = *ev_poll_vtable;
  vtable.shutdown_engine = shutdown_engine;
  return &vtable;
}

#endif /* GPR_POSIX_SOCKET */
