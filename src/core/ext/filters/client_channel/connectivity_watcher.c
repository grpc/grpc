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

#include "src/core/ext/filters/client_channel/connectivity_watcher.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include "src/core/ext/filters/client_channel/channel_connectivity_internal.h"
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"

#define DEFAULT_CONNECTIVITY_CHECK_INTERVAL_MS 500

typedef struct connectivity_watcher {
  grpc_timer watcher_timer;
  grpc_closure check_connectivity_closure;
  grpc_completion_queue* cq;
  gpr_refcount refs;
  size_t channel_count;
  bool shutting_down;
} connectivity_watcher;

typedef struct channel_state {
  grpc_channel_element* client_channel_elem;
  grpc_channel_stack* channel_stack;
  grpc_connectivity_state state;
} channel_state;

static gpr_once g_once = GPR_ONCE_INIT;
static gpr_mu g_watcher_mu;
static connectivity_watcher* g_watcher = NULL;

static void init_g_watcher_mu() { gpr_mu_init(&g_watcher_mu); }

static void start_watching_locked(grpc_exec_ctx* exec_ctx,
                                  grpc_channel_element* client_channel_elem,
                                  grpc_channel_stack* channel_stack) {
  gpr_ref(&g_watcher->refs);
  ++g_watcher->channel_count;
  channel_state* s = (channel_state*)gpr_zalloc(sizeof(channel_state));
  s->client_channel_elem = client_channel_elem;
  s->channel_stack = channel_stack;
  s->state = GRPC_CHANNEL_IDLE;
  grpc_channel_watch_connectivity_state_internal(
      exec_ctx, client_channel_elem, channel_stack, s->state,
      gpr_inf_future(GPR_CLOCK_MONOTONIC), g_watcher->cq, (void*)s);
}

static bool is_disabled() {
  char* env = gpr_getenv("GRPC_DISABLE_CHANNEL_CONNECTIVITY_WATCHER");
  bool disabled = gpr_is_true(env);
  gpr_free(env);
  return disabled;
}

static bool connectivity_watcher_unref(grpc_exec_ctx* exec_ctx) {
  if (gpr_unref(&g_watcher->refs)) {
    gpr_mu_lock(&g_watcher_mu);
    grpc_completion_queue_destroy(g_watcher->cq);
    gpr_free(g_watcher);
    g_watcher = NULL;
    gpr_mu_unlock(&g_watcher_mu);
    return true;
  }
  return false;
}

static void check_connectivity_state(grpc_exec_ctx* exec_ctx, void* ignored,
                                     grpc_error* error) {
  grpc_event ev;
  while (true) {
    gpr_mu_lock(&g_watcher_mu);
    if (g_watcher->shutting_down) {
      // Drain cq if the watcher is shutting down
      ev = grpc_completion_queue_next(
          g_watcher->cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL);
    } else {
      ev = grpc_completion_queue_next(g_watcher->cq,
                                      gpr_inf_past(GPR_CLOCK_MONOTONIC), NULL);
      // Make sure we've seen 2 TIMEOUTs before going to sleep
      if (ev.type == GRPC_QUEUE_TIMEOUT) {
        ev = grpc_completion_queue_next(
            g_watcher->cq, gpr_inf_past(GPR_CLOCK_MONOTONIC), NULL);
        if (ev.type == GRPC_QUEUE_TIMEOUT) {
          gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
          grpc_timer_init(
              exec_ctx, &g_watcher->watcher_timer,
              gpr_time_add(now, gpr_time_from_millis(
                                    DEFAULT_CONNECTIVITY_CHECK_INTERVAL_MS,
                                    GPR_TIMESPAN)),
              &g_watcher->check_connectivity_closure, now);
          gpr_mu_unlock(&g_watcher_mu);
          break;
        }
      }
    }
    gpr_mu_unlock(&g_watcher_mu);
    if (ev.type != GRPC_OP_COMPLETE) {
      break;
    }
    channel_state* s = (channel_state*)(ev.tag);
    s->state = grpc_client_channel_check_connectivity_state(
        exec_ctx, s->client_channel_elem, false /* try_to_connect */);
    if (s->state == GRPC_CHANNEL_SHUTDOWN) {
      GRPC_CHANNEL_STACK_UNREF(exec_ctx, s->channel_stack,
                               "connectivity_watcher_stop_watching");
      gpr_free(s);
      if (connectivity_watcher_unref(exec_ctx)) {
        break;
      }
    } else {
      grpc_channel_watch_connectivity_state_internal(
          exec_ctx, s->client_channel_elem, s->channel_stack, s->state,
          gpr_inf_future(GPR_CLOCK_MONOTONIC), g_watcher->cq, s);
    }
  }
}

void grpc_client_channel_start_watching_connectivity(
    grpc_exec_ctx* exec_ctx, grpc_channel_element* client_channel_elem,
    grpc_channel_stack* channel_stack) {
  if (is_disabled()) return;
  GRPC_CHANNEL_STACK_REF(channel_stack, "connectivity_watcher_start_watching");
  gpr_once_init(&g_once, init_g_watcher_mu);
  gpr_mu_lock(&g_watcher_mu);
  if (g_watcher == NULL) {
    g_watcher = (connectivity_watcher*)gpr_zalloc(sizeof(connectivity_watcher));
    g_watcher->cq = grpc_completion_queue_create_internal(
        GRPC_CQ_NEXT, GRPC_CQ_DEFAULT_POLLING);
    gpr_ref_init(&g_watcher->refs, 0);
    GRPC_CLOSURE_INIT(&g_watcher->check_connectivity_closure,
                      check_connectivity_state, NULL,
                      grpc_schedule_on_exec_ctx);
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    grpc_timer_init(
        exec_ctx, &g_watcher->watcher_timer,
        gpr_time_add(
            now, gpr_time_from_millis(DEFAULT_CONNECTIVITY_CHECK_INTERVAL_MS,
                                      GPR_TIMESPAN)),
        &g_watcher->check_connectivity_closure, now);
  }
  start_watching_locked(exec_ctx, client_channel_elem, channel_stack);
  gpr_mu_init(&g_watcher_mu);
}

void grpc_client_channel_stop_watching_connectivity(
    grpc_exec_ctx* exec_ctx, grpc_channel_element* client_channel_elem,
    grpc_channel_stack* channel_stack) {
  if (is_disabled()) return;
  gpr_once_init(&g_once, init_g_watcher_mu);
  gpr_mu_lock(&g_watcher_mu);
  if (--g_watcher->channel_count == 0) {
    g_watcher->shutting_down = true;
    grpc_timer_cancel(exec_ctx, &g_watcher->watcher_timer);
    connectivity_watcher_unref(exec_ctx);
  }
  gpr_mu_unlock(&g_watcher_mu);
}
