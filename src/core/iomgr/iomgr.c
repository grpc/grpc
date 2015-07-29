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

#include "src/core/iomgr/iomgr.h"

#include <stdlib.h>

#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/alarm_internal.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>

#ifdef GRPC_IOMGR_REFCOUNT_DEBUG
static void iomgr_object_trace(const char *action, grpc_iomgr_object *obj,
                               const char *file, int line) {
  gpr_log(GPR_DEBUG, "iomgr object %s: %p - %s from %s:%i", action,
          obj, obj->name, file, line);
}
#else
#define iomgr_object_trace(action, obj, file, line)
#endif

static gpr_mu g_mu;
static gpr_cv g_rcv;
static grpc_iomgr_closure *g_cbs_head = NULL;
static grpc_iomgr_closure *g_cbs_tail = NULL;
static int g_shutdown;
static gpr_event g_background_callback_executor_done;
static grpc_iomgr_object g_root_object;

/* Execute followup callbacks continuously.
   Other threads may check in and help during pollset_work() */
static void background_callback_executor(void *ignored) {
  gpr_mu_lock(&g_mu);
  while (!g_shutdown) {
    gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
    gpr_timespec short_deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_millis(100, GPR_TIMESPAN));
    if (g_cbs_head) {
      grpc_iomgr_closure *closure = g_cbs_head;
      g_cbs_head = closure->next;
      if (!g_cbs_head) g_cbs_tail = NULL;
      gpr_mu_unlock(&g_mu);
      closure->cb(closure->cb_arg, closure->success);
      gpr_mu_lock(&g_mu);
    } else if (grpc_alarm_check(&g_mu, gpr_now(GPR_CLOCK_MONOTONIC),
                                &deadline)) {
    } else {
      gpr_mu_unlock(&g_mu);
      gpr_sleep_until(gpr_time_min(short_deadline, deadline));
      gpr_mu_lock(&g_mu);
    }
  }
  gpr_mu_unlock(&g_mu);
  gpr_event_set(&g_background_callback_executor_done, (void *)1);
}

void grpc_kick_poller(void) {
  /* Empty. The background callback executor polls periodically. The activity
   * the kicker is trying to draw the executor's attention to will be picked up
   * either by one of the periodic wakeups or by one of the polling application
   * threads. */
}

void grpc_iomgr_init(void) {
  gpr_thd_id id;
  g_shutdown = 0;
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_rcv);
  grpc_alarm_list_init(gpr_now(GPR_CLOCK_MONOTONIC));
  g_root_object.next = g_root_object.prev = &g_root_object;
  g_root_object.name = "root";
  grpc_iomgr_platform_init();
  gpr_event_init(&g_background_callback_executor_done);
  gpr_thd_new(&id, background_callback_executor, NULL, NULL);
}

static size_t count_objects(void) {
  grpc_iomgr_object *obj;
  size_t n = 0;
  for (obj = g_root_object.next; obj != &g_root_object; obj = obj->next) {
    n++;
  }
  return n;
}

void grpc_iomgr_shutdown(void) {
  grpc_iomgr_object *obj;
  grpc_iomgr_closure *closure;
  gpr_timespec shutdown_deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(10, GPR_TIMESPAN));
  gpr_timespec last_warning_time = gpr_now(GPR_CLOCK_REALTIME);

  gpr_mu_lock(&g_mu);
  g_shutdown = 1;
  while (g_cbs_head != NULL || g_root_object.next != &g_root_object) {
    if (gpr_time_cmp(
            gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), last_warning_time),
            gpr_time_from_seconds(1, GPR_TIMESPAN)) >= 0) {
      if (g_cbs_head != NULL && g_root_object.next != &g_root_object) {
        gpr_log(GPR_DEBUG,
                "Waiting for %d iomgr objects to be destroyed and executing "
                "final callbacks",
                count_objects());
      } else if (g_cbs_head != NULL) {
        gpr_log(GPR_DEBUG, "Executing final iomgr callbacks");
      } else {
        gpr_log(GPR_DEBUG, "Waiting for %d iomgr objects to be destroyed",
                count_objects());
      }
      last_warning_time = gpr_now(GPR_CLOCK_REALTIME);
    }
    if (g_cbs_head) {
      do {
        closure = g_cbs_head;
        g_cbs_head = closure->next;
        if (!g_cbs_head) g_cbs_tail = NULL;
        gpr_mu_unlock(&g_mu);

        closure->cb(closure->cb_arg, 0);
        gpr_mu_lock(&g_mu);
      } while (g_cbs_head);
      continue;
    }
    if (grpc_alarm_check(&g_mu, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL)) {
      continue;
    }
    if (g_root_object.next != &g_root_object) {
      int timeout = 0;
      gpr_timespec short_deadline = gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(100, GPR_TIMESPAN));
      while (gpr_cv_wait(&g_rcv, &g_mu, short_deadline) && g_cbs_head == NULL) {
        if (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), shutdown_deadline) > 0) {
          timeout = 1;
          break;
        }
      }
      if (timeout) {
        gpr_log(GPR_DEBUG,
                "Failed to free %d iomgr objects before shutdown deadline: "
                "memory leaks are likely",
                count_objects());
        for (obj = g_root_object.next; obj != &g_root_object; obj = obj->next) {
          gpr_log(GPR_DEBUG, "LEAKED OBJECT: %s %p", obj->name, obj);
        }
        break;
      }
    }
  }
  gpr_mu_unlock(&g_mu);

  grpc_kick_poller();
  gpr_event_wait(&g_background_callback_executor_done,
                 gpr_inf_future(GPR_CLOCK_REALTIME));

  grpc_alarm_list_shutdown();

  grpc_iomgr_platform_shutdown();
  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_rcv);
}

void grpc_iomgr_register_object_internal(grpc_iomgr_object *obj,
                                         const char *name,
                                         const char *file,
                                         int line) {
  obj->name = gpr_strdup(name);
  iomgr_object_trace("register", obj, file, line);
  gpr_mu_lock(&g_mu);
  obj->next = &g_root_object;
  obj->prev = obj->next->prev;
  obj->next->prev = obj->prev->next = obj;
  gpr_mu_unlock(&g_mu);
}

void grpc_iomgr_unregister_object_internal(grpc_iomgr_object *obj,
                                           const char *file,
                                           int line) {
  iomgr_object_trace("unregister", obj, file, line);
  gpr_mu_lock(&g_mu);
  obj->next->prev = obj->prev;
  obj->prev->next = obj->next;
  gpr_cv_signal(&g_rcv);
  gpr_mu_unlock(&g_mu);
  gpr_free(obj->name);
}

void grpc_iomgr_closure_init(grpc_iomgr_closure *closure, grpc_iomgr_cb_func cb,
                             void *cb_arg) {
  closure->cb = cb;
  closure->cb_arg = cb_arg;
  closure->next = NULL;
}

static void assert_not_scheduled_locked(grpc_iomgr_closure *closure) {
#ifndef NDEBUG
  grpc_iomgr_closure *c;

  for (c = g_cbs_head; c; c = c->next) {
    GPR_ASSERT(c != closure);
  }
#endif
}

void grpc_iomgr_add_delayed_callback(grpc_iomgr_closure *closure, int success) {
  closure->success = success;
  GPR_ASSERT(closure->cb);
  gpr_mu_lock(&g_mu);
  assert_not_scheduled_locked(closure);
  closure->next = NULL;
  if (!g_cbs_tail) {
    g_cbs_head = g_cbs_tail = closure;
  } else {
    g_cbs_tail->next = closure;
    g_cbs_tail = closure;
  }
  if (g_shutdown) {
    gpr_cv_signal(&g_rcv);
  }
  gpr_mu_unlock(&g_mu);
}

void grpc_iomgr_add_callback(grpc_iomgr_closure *closure) {
  grpc_iomgr_add_delayed_callback(closure, 1 /* GPR_TRUE */);
}

int grpc_maybe_call_delayed_callbacks(gpr_mu *drop_mu, int success) {
  int n = 0;
  gpr_mu *retake_mu = NULL;
  grpc_iomgr_closure *closure;
  for (;;) {
    /* check for new work */
    if (!gpr_mu_trylock(&g_mu)) {
      break;
    }
    closure = g_cbs_head;
    if (!closure) {
      gpr_mu_unlock(&g_mu);
      break;
    }
    g_cbs_head = closure->next;
    if (!g_cbs_head) g_cbs_tail = NULL;
    gpr_mu_unlock(&g_mu);
    /* if we have a mutex to drop, do so before executing work */
    if (drop_mu) {
      gpr_mu_unlock(drop_mu);
      retake_mu = drop_mu;
      drop_mu = NULL;
    }
    closure->cb(closure->cb_arg, success && closure->success);
    n++;
  }
  if (retake_mu) {
    gpr_mu_lock(retake_mu);
  }
  return n;
}
