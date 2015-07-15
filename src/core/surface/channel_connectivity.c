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

#include "src/core/surface/channel.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/channel/client_channel.h"
#include "src/core/iomgr/alarm.h"
#include "src/core/surface/completion_queue.h"

grpc_connectivity_state grpc_channel_check_connectivity_state(
    grpc_channel *channel, int try_to_connect) {
  /* forward through to the underlying client channel */
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  if (client_channel_elem->filter != &grpc_client_channel_filter) {
    gpr_log(GPR_ERROR,
            "grpc_channel_check_connectivity_state called on something that is "
            "not a client channel, but '%s'",
            client_channel_elem->filter->name);
    return GRPC_CHANNEL_FATAL_FAILURE;
  }
  return grpc_client_channel_check_connectivity_state(client_channel_elem,
                                                      try_to_connect);
}

typedef enum {
  WAITING,
  CALLING_BACK,
  CALLING_BACK_AND_FINISHED,
  CALLED_BACK
} callback_phase;

typedef struct {
  gpr_mu mu;
  callback_phase phase;
  int success;
  grpc_iomgr_closure on_complete;
  grpc_alarm alarm;
  grpc_connectivity_state state;
  grpc_connectivity_state *optional_new_state;
  grpc_completion_queue *cq;
  grpc_cq_completion completion_storage;
  void *tag;
} state_watcher;

static void delete_state_watcher(state_watcher *w) {
  gpr_mu_destroy(&w->mu);
  gpr_free(w);
}

static void finished_completion(void *pw, grpc_cq_completion *ignored) {
  int delete = 0;
  state_watcher *w = pw;
  gpr_mu_lock(&w->mu);
  switch (w->phase) {
    case WAITING:
    case CALLED_BACK:
      gpr_log(GPR_ERROR, "should never reach here");
      abort();
      break;
    case CALLING_BACK:
      w->phase = CALLED_BACK;
      break;
    case CALLING_BACK_AND_FINISHED:
      delete = 1;
      break;
  }
  gpr_mu_unlock(&w->mu);

  if (delete) {
    delete_state_watcher(w);
  }
}

static void partly_done(state_watcher *w, int due_to_completion) {
  int delete = 0;

  if (due_to_completion) {
    gpr_mu_lock(&w->mu);
    w->success = 1;
    gpr_mu_unlock(&w->mu);
    grpc_alarm_cancel(&w->alarm);
  }

  gpr_mu_lock(&w->mu);
  switch (w->phase) {
    case WAITING:
      w->phase = CALLING_BACK;
      if (w->optional_new_state) {
        *w->optional_new_state = w->state;
      }
      grpc_cq_end_op(w->cq, w->tag, w->success, finished_completion, w,
                     &w->completion_storage);
      break;
    case CALLING_BACK:
      w->phase = CALLING_BACK_AND_FINISHED;
      break;
    case CALLING_BACK_AND_FINISHED:
      gpr_log(GPR_ERROR, "should never reach here");
      abort();
      break;
    case CALLED_BACK:
      delete = 1;
      break;
  }
  gpr_mu_unlock(&w->mu);

  if (delete) {
    delete_state_watcher(w);
  }
}

static void watch_complete(void *pw, int success) { partly_done(pw, 0); }

static void timeout_complete(void *pw, int success) { partly_done(pw, 1); }

void grpc_channel_watch_connectivity_state(
    grpc_channel *channel, grpc_connectivity_state last_observed_state,
    grpc_connectivity_state *optional_new_state, gpr_timespec deadline,
    grpc_completion_queue *cq, void *tag) {
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  state_watcher *w = gpr_malloc(sizeof(*w));

  grpc_cq_begin_op(cq);

  gpr_mu_init(&w->mu);
  grpc_iomgr_closure_init(&w->on_complete, watch_complete, w);
  w->phase = WAITING;
  w->state = last_observed_state;
  w->success = 0;
  w->optional_new_state = optional_new_state;
  w->cq = cq;
  w->tag = tag;

  grpc_alarm_init(&w->alarm, deadline, timeout_complete, w,
                  gpr_now(GPR_CLOCK_REALTIME));

  if (client_channel_elem->filter != &grpc_client_channel_filter) {
    gpr_log(GPR_ERROR,
            "grpc_channel_watch_connectivity_state called on something that is "
            "not a client channel, but '%s'",
            client_channel_elem->filter->name);
    grpc_iomgr_add_delayed_callback(&w->on_complete, 1);
  } else {
    grpc_client_channel_watch_connectivity_state(client_channel_elem, &w->state,
                                                 &w->on_complete);
  }
}
