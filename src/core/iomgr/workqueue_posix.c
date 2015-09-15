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
  workqueue->wakeup_read_fd =
      grpc_fd_create(GRPC_WAKEUP_FD_GET_READ_FD(&workqueue->wakeup_fd), name);
  grpc_iomgr_closure_init(&workqueue->read_closure, on_readable, workqueue);
  grpc_fd_notify_on_read(workqueue->wakeup_read_fd, &workqueue->read_closure);
  return workqueue;
}

static void workqueue_destroy(grpc_workqueue *workqueue) {
  grpc_fd_shutdown(workqueue->wakeup_read_fd);
}

void grpc_workqueue_ref(grpc_workqueue *workqueue) {
  gpr_ref(&workqueue->refs);
}

void grpc_workqueue_unref(grpc_workqueue *workqueue) {
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
  gpr_mu_unlock(&workqueue->mu);
}

#endif /* GPR_POSIX_SOCKET */
