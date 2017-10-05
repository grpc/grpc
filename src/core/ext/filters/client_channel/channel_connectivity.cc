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

#include "src/core/lib/surface/channel.h"

#include <inttypes.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/channel_connectivity_internal.h"
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/completion_queue.h"

grpc_connectivity_state grpc_channel_check_connectivity_state(
    grpc_channel *channel, int try_to_connect) {
  /* forward through to the underlying client channel */
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_connectivity_state state;
  GRPC_API_TRACE(
      "grpc_channel_check_connectivity_state(channel=%p, try_to_connect=%d)", 2,
      (channel, try_to_connect));
  if (client_channel_elem->filter == &grpc_client_channel_filter) {
    state = grpc_client_channel_check_connectivity_state(
        &exec_ctx, client_channel_elem, try_to_connect);
    grpc_exec_ctx_finish(&exec_ctx);
    return state;
  }
  gpr_log(GPR_ERROR,
          "grpc_channel_check_connectivity_state called on something that is "
          "not a client channel, but '%s'",
          client_channel_elem->filter->name);
  grpc_exec_ctx_finish(&exec_ctx);
  return GRPC_CHANNEL_SHUTDOWN;
}

int grpc_channel_num_external_connectivity_watchers(grpc_channel *channel) {
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  return grpc_client_channel_num_external_connectivity_watchers(
      client_channel_elem);
}

typedef struct watcher_timer_init_arg {
  state_watcher *w;
  gpr_timespec deadline;
} watcher_timer_init_arg;

static void watcher_timer_init(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error_ignored) {
  watcher_timer_init_arg *wa = (watcher_timer_init_arg *)arg;

  grpc_timer_init(exec_ctx, &wa->w->alarm,
                  grpc_timespec_to_millis_round_up(wa->deadline),
                  &wa->w->on_timeout);
  gpr_free(wa);
}

int grpc_channel_support_connectivity_watcher(grpc_channel *channel) {
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  return client_channel_elem->filter != &grpc_client_channel_filter ? 0 : 1;
}

void grpc_channel_watch_connectivity_state(
    grpc_channel *channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue *cq, void *tag) {
  grpc_channel_stack *channel_stack = grpc_channel_get_channel_stack(channel);
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(channel_stack);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE(
      "grpc_channel_watch_connectivity_state("
      "channel=%p, last_observed_state=%d, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "cq=%p, tag=%p)",
      7, (channel, (int)last_observed_state, deadline.tv_sec, deadline.tv_nsec,
          (int)deadline.clock_type, cq, tag));
  grpc_channel_watch_connectivity_state_internal(
      &exec_ctx, client_channel_elem, channel_stack, last_observed_state,
      deadline, cq, tag);
  grpc_exec_ctx_finish(&exec_ctx);
}
