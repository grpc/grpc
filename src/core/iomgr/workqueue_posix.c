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

#ifdef GPR_POSIX_SOCKET

#include "src/core/iomgr/workqueue.h"

#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#include "src/core/iomgr/fd_posix.h"

static void on_readable(void *arg, int success);

grpc_workqueue *grpc_workqueue_create(void) {
  char name[32];
  grpc_workqueue *workqueue = gpr_malloc(sizeof(grpc_workqueue));
  gpr_ref_init(&workqueue->refs, 1);
  gpr_mu_init(&workqueue->mu);
  workqueue->head.next = NULL;
  workqueue->tail = &workqueue->head;
  grpc_wakeup_fd_init(&workqueue->wakeup_fd);
  sprintf(name, "workqueue:%p", (void *)workqueue);
  workqueue->wakeup_read_fd = NULL; /* inspected during grpc_fd_create below */
  workqueue->wakeup_read_fd = grpc_fd_create(
      GRPC_WAKEUP_FD_GET_READ_FD(&workqueue->wakeup_fd), workqueue, name);
  grpc_iomgr_closure_init(&workqueue->read_closure, on_readable, workqueue);
  grpc_fd_notify_on_read(workqueue->wakeup_read_fd, &workqueue->read_closure);
  return workqueue;
}

static void shutdown_thread(void *arg) {
  grpc_iomgr_closure *todo = arg;

  while (todo) {
    grpc_iomgr_closure *next = todo->next;
    todo->cb(todo->cb_arg, todo->success);
    todo = next;
  }
}

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
static size_t count_waiting(grpc_workqueue *workqueue) {
  size_t i = 0;
  grpc_iomgr_closure *c;
  for (c = workqueue->head.next; c; c = c->next) {
    i++;
  }
  return i;
}
#endif

void grpc_workqueue_flush(grpc_workqueue *workqueue, int asynchronously) {
  grpc_iomgr_closure *todo;
  gpr_thd_id thd;

  gpr_mu_lock(&workqueue->mu);
#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
  gpr_log(GPR_DEBUG, "WORKQUEUE:%p flush %d objects %s", workqueue,
          count_waiting(workqueue),
          asynchronously ? "asynchronously" : "synchronously");
#endif
  todo = workqueue->head.next;
  workqueue->head.next = NULL;
  workqueue->tail = &workqueue->head;
  gpr_mu_unlock(&workqueue->mu);

  if (todo != NULL) {
    if (asynchronously) {
      gpr_thd_new(&thd, shutdown_thread, todo, NULL);
    } else {
      while (todo) {
        grpc_iomgr_closure *next = todo->next;
        todo->cb(todo->cb_arg, todo->success);
        todo = next;
      }
    }
  }
}

static void workqueue_destroy(grpc_workqueue *workqueue) {
  GPR_ASSERT(workqueue->tail == &workqueue->head);
  grpc_fd_shutdown(workqueue->wakeup_read_fd);
}

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
void grpc_workqueue_ref(grpc_workqueue *workqueue, const char *file, int line,
                        const char *reason) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "WORKQUEUE:%p   ref %d -> %d %s",
          workqueue, (int)workqueue->refs.count, (int)workqueue->refs.count + 1,
          reason);
#else
void grpc_workqueue_ref(grpc_workqueue *workqueue) {
#endif
  gpr_ref(&workqueue->refs);
}

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
void grpc_workqueue_unref(grpc_workqueue *workqueue, const char *file, int line,
                          const char *reason) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "WORKQUEUE:%p unref %d -> %d %s",
          workqueue, (int)workqueue->refs.count, (int)workqueue->refs.count - 1,
          reason);
#else
void grpc_workqueue_unref(grpc_workqueue *workqueue) {
#endif
  if (gpr_unref(&workqueue->refs)) {
    workqueue_destroy(workqueue);
  }
}

void grpc_workqueue_add_to_pollset(grpc_workqueue *workqueue,
                                   grpc_pollset *pollset) {
  grpc_pollset_add_fd(pollset, workqueue->wakeup_read_fd);
}

static void on_readable(void *arg, int success) {
  grpc_workqueue *workqueue = arg;
  grpc_iomgr_closure *todo;

  if (!success) {
    gpr_mu_destroy(&workqueue->mu);
    /* HACK: let wakeup_fd code know that we stole the fd */
    workqueue->wakeup_fd.read_fd = 0;
    grpc_wakeup_fd_destroy(&workqueue->wakeup_fd);
    grpc_fd_orphan(workqueue->wakeup_read_fd, NULL, "destroy");
    gpr_free(workqueue);
    return;
  } else {
    gpr_mu_lock(&workqueue->mu);
#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
    gpr_log(GPR_DEBUG, "WORKQUEUE:%p %d objects", workqueue,
            count_waiting(workqueue));
#endif
    todo = workqueue->head.next;
    workqueue->head.next = NULL;
    workqueue->tail = &workqueue->head;
    grpc_wakeup_fd_consume_wakeup(&workqueue->wakeup_fd);
    gpr_mu_unlock(&workqueue->mu);
    grpc_fd_notify_on_read(workqueue->wakeup_read_fd, &workqueue->read_closure);

    while (todo) {
      grpc_iomgr_closure *next = todo->next;
      todo->cb(todo->cb_arg, todo->success);
      todo = next;
    }
  }
}

void grpc_workqueue_push(grpc_workqueue *workqueue, grpc_iomgr_closure *closure,
                         int success) {
  closure->success = success;
  closure->next = NULL;
  gpr_mu_lock(&workqueue->mu);
  if (workqueue->tail == &workqueue->head) {
    grpc_wakeup_fd_wakeup(&workqueue->wakeup_fd);
  }
  workqueue->tail->next = closure;
  workqueue->tail = closure;
#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
  gpr_log(GPR_DEBUG, "WORKQUEUE:%p %d objects", workqueue,
          count_waiting(workqueue));
#endif
  gpr_mu_unlock(&workqueue->mu);
}

#endif /* GPR_POSIX_SOCKET */
