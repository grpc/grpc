/*
 *
 * Copyright 2015, Google Inc.
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

#ifdef GPR_WINSOCK_SOCKET

#include <grpc/support/thd.h>

#include "src/core/iomgr/alarm_internal.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/pollset.h"
#include "src/core/iomgr/pollset_windows.h"

static void remove_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->prev->next = worker->next;
  worker->next->prev = worker->prev;
}

static int has_workers(grpc_pollset *p) {
  return p->root_worker.next != &p->root_worker;
}

static grpc_pollset_worker *pop_front_worker(grpc_pollset *p) {
  if (has_workers(p)) {
    grpc_pollset_worker *w = p->root_worker.next;
    remove_worker(p, w);
    return w;
  }
  else {
    return NULL;
  }
}

static void push_back_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->next = &p->root_worker;
  worker->prev = worker->next->prev;
  worker->prev->next = worker->next->prev = worker;
}

static void push_front_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->prev = &p->root_worker;
  worker->next = worker->prev->next;
  worker->prev->next = worker->next->prev = worker;
}

/* There isn't really any such thing as a pollset under Windows, due to the
   nature of the IO completion ports. We're still going to provide a minimal
   set of features for the sake of the rest of grpc. But grpc_pollset_work
   won't actually do any polling, and return as quickly as possible. */

void grpc_pollset_init(grpc_pollset *pollset) {
  memset(pollset, 0, sizeof(*pollset));
  gpr_mu_init(&pollset->mu);
  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->kicked_without_pollers = 0;
}

void grpc_pollset_shutdown(grpc_pollset *pollset,
                           void (*shutdown_done)(void *arg),
                           void *shutdown_done_arg) {
  gpr_mu_lock(&pollset->mu);
  pollset->shutting_down = 1;
  grpc_pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);
  gpr_mu_unlock(&pollset->mu);
  shutdown_done(shutdown_done_arg);
}

void grpc_pollset_destroy(grpc_pollset *pollset) {
  gpr_mu_destroy(&pollset->mu);
}

void grpc_pollset_work(grpc_pollset *pollset, grpc_pollset_worker *worker, 
                       gpr_timespec now, gpr_timespec deadline) {
  int added_worker = 0;
  worker->next = worker->prev = NULL;
  gpr_cv_init(&worker->cv);
  if (grpc_maybe_call_delayed_callbacks(&pollset->mu, 1 /* GPR_TRUE */)) {
    goto done;
  }
  if (grpc_alarm_check(&pollset->mu, now, &deadline)) {
    goto done;
  }
  if (!pollset->kicked_without_pollers && !pollset->shutting_down) {
    push_front_worker(pollset, worker);
    added_worker = 1;
    gpr_cv_wait(&worker->cv, &pollset->mu, deadline);
  } else {
    pollset->kicked_without_pollers = 0;
  }
done:
  gpr_cv_destroy(&worker->cv);
  if (added_worker) {
    remove_worker(pollset, worker);
  }
}

void grpc_pollset_kick(grpc_pollset *p, grpc_pollset_worker *specific_worker) {
  if (specific_worker != NULL) {
    if (specific_worker == GRPC_POLLSET_KICK_BROADCAST) {
      for (specific_worker = p->root_worker.next;
        specific_worker != &p->root_worker;
        specific_worker = specific_worker->next) {
        gpr_cv_signal(&specific_worker->cv);
      }
      p->kicked_without_pollers = 1;
    } else {
      gpr_cv_signal(&specific_worker->cv);
    }
  } else {
    specific_worker = pop_front_worker(p);
    if (specific_worker != NULL) {
      push_back_worker(p, specific_worker);
      gpr_cv_signal(&specific_worker->cv);
    } else {
      p->kicked_without_pollers = 1;
    }
  }
}

#endif /* GPR_WINSOCK_SOCKET */
