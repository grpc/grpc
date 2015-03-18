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

#include "src/core/channel/client_setup.h"
#include "src/core/channel/channel_args.h"
#include "src/core/channel/channel_stack.h"
#include "src/core/iomgr/alarm.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

struct grpc_client_setup {
  grpc_transport_setup base; /* must be first */
  void (*initiate)(void *user_data, grpc_client_setup_request *request);
  void (*done)(void *user_data);
  void *user_data;
  grpc_channel_args *args;
  grpc_mdctx *mdctx;
  grpc_alarm backoff_alarm;
  gpr_timespec current_backoff_interval;
  int in_alarm;
  int in_cb;
  int cancelled;

  gpr_mu mu;
  gpr_cv cv;
  grpc_client_setup_request *active_request;
  int refs;
};

struct grpc_client_setup_request {
  /* pointer back to the setup object */
  grpc_client_setup *setup;
  gpr_timespec deadline;
};

gpr_timespec grpc_client_setup_request_deadline(grpc_client_setup_request *r) {
  return r->deadline;
}

static void destroy_setup(grpc_client_setup *s) {
  gpr_mu_destroy(&s->mu);
  gpr_cv_destroy(&s->cv);
  s->done(s->user_data);
  grpc_channel_args_destroy(s->args);
  gpr_free(s);
}

/* initiate handshaking */
static void setup_initiate(grpc_transport_setup *sp) {
  grpc_client_setup *s = (grpc_client_setup *)sp;
  grpc_client_setup_request *r = gpr_malloc(sizeof(grpc_client_setup_request));
  int in_alarm = 0;

  r->setup = s;
  /* TODO(klempner): Actually set a deadline */
  r->deadline = gpr_inf_future;

  gpr_mu_lock(&s->mu);
  GPR_ASSERT(s->refs > 0);
  /* there might be more than one request outstanding if the caller calls
     initiate in some kind of rapid-fire way: we try to connect each time,
     and keep track of the latest request (which is the only one that gets
     to finish) */
  if (!s->in_alarm) {
    s->active_request = r;
    s->refs++;
  } else {
    /* TODO(klempner): Maybe do something more clever here */
    in_alarm = 1;
  }
  gpr_mu_unlock(&s->mu);

  if (!in_alarm) {
    s->initiate(s->user_data, r);
  } else {
    gpr_free(r);
  }
}

/* cancel handshaking: cancel all requests, and shutdown (the caller promises
   not to initiate again) */
static void setup_cancel(grpc_transport_setup *sp) {
  grpc_client_setup *s = (grpc_client_setup *)sp;
  int cancel_alarm = 0;

  gpr_mu_lock(&s->mu);
  s->cancelled = 1;
  while (s->in_cb) {
    gpr_cv_wait(&s->cv, &s->mu, gpr_inf_future);
  }

  GPR_ASSERT(s->refs > 0);
  /* effectively cancels the current request (if any) */
  s->active_request = NULL;
  if (s->in_alarm) {
    cancel_alarm = 1;
  }
  if (--s->refs == 0) {
    gpr_mu_unlock(&s->mu);
    destroy_setup(s);
  } else {
    gpr_mu_unlock(&s->mu);
  }
  if (cancel_alarm) {
    grpc_alarm_cancel(&s->backoff_alarm);
  }
}

int grpc_client_setup_cb_begin(grpc_client_setup_request *r) {
  gpr_mu_lock(&r->setup->mu);
  if (r->setup->cancelled) {
    gpr_mu_unlock(&r->setup->mu);
    return 0;
  }
  r->setup->in_cb++;
  gpr_mu_unlock(&r->setup->mu);
  return 1;
}

void grpc_client_setup_cb_end(grpc_client_setup_request *r) {
  gpr_mu_lock(&r->setup->mu);
  r->setup->in_cb--;
  if (r->setup->cancelled) gpr_cv_signal(&r->setup->cv);
  gpr_mu_unlock(&r->setup->mu);
}

/* vtable for transport setup */
static const grpc_transport_setup_vtable setup_vtable = {setup_initiate,
                                                         setup_cancel};

void grpc_client_setup_create_and_attach(
    grpc_channel_stack *newly_minted_channel, const grpc_channel_args *args,
    grpc_mdctx *mdctx,
    void (*initiate)(void *user_data, grpc_client_setup_request *request),
    void (*done)(void *user_data), void *user_data) {
  grpc_client_setup *s = gpr_malloc(sizeof(grpc_client_setup));

  s->base.vtable = &setup_vtable;
  gpr_mu_init(&s->mu);
  gpr_cv_init(&s->cv);
  s->refs = 1;
  s->mdctx = mdctx;
  s->initiate = initiate;
  s->done = done;
  s->user_data = user_data;
  s->active_request = NULL;
  s->args = grpc_channel_args_copy(args);
  s->current_backoff_interval = gpr_time_from_micros(1000000);
  s->in_alarm = 0;
  s->in_cb = 0;
  s->cancelled = 0;

  grpc_client_channel_set_transport_setup(newly_minted_channel, &s->base);
}

int grpc_client_setup_request_should_continue(grpc_client_setup_request *r) {
  int result;
  if (gpr_time_cmp(gpr_now(), r->deadline) > 0) {
    return 0;
  }
  gpr_mu_lock(&r->setup->mu);
  result = r->setup->active_request == r;
  gpr_mu_unlock(&r->setup->mu);
  return result;
}

static void backoff_alarm_done(void *arg /* grpc_client_setup */, int success) {
  grpc_client_setup *s = arg;
  grpc_client_setup_request *r = gpr_malloc(sizeof(grpc_client_setup_request));
  r->setup = s;
  /* TODO(klempner): Set this to something useful */
  r->deadline = gpr_inf_future;
  /* Handle status cancelled? */
  gpr_mu_lock(&s->mu);
  s->active_request = r;
  s->in_alarm = 0;
  if (!success) {
    if (0 == --s->refs) {
      gpr_mu_unlock(&s->mu);
      destroy_setup(s);
      gpr_free(r);
      return;
    } else {
      gpr_mu_unlock(&s->mu);
      return;
    }
  }
  gpr_mu_unlock(&s->mu);
  s->initiate(s->user_data, r);
}

void grpc_client_setup_request_finish(grpc_client_setup_request *r,
                                      int was_successful) {
  int retry = !was_successful;
  grpc_client_setup *s = r->setup;

  gpr_mu_lock(&s->mu);
  if (s->active_request == r) {
    s->active_request = NULL;
  } else {
    retry = 0;
  }
  if (!retry && 0 == --s->refs) {
    gpr_mu_unlock(&s->mu);
    destroy_setup(s);
    gpr_free(r);
    return;
  }

  gpr_free(r);

  if (retry) {
    /* TODO(klempner): Replace these values with further consideration. 2x is
       probably too aggressive of a backoff. */
    gpr_timespec max_backoff = gpr_time_from_minutes(2);
    gpr_timespec now = gpr_now();
    gpr_timespec deadline = gpr_time_add(s->current_backoff_interval, now);
    GPR_ASSERT(!s->in_alarm);
    s->in_alarm = 1;
    grpc_alarm_init(&s->backoff_alarm, deadline, backoff_alarm_done, s, now);
    s->current_backoff_interval =
        gpr_time_add(s->current_backoff_interval, s->current_backoff_interval);
    if (gpr_time_cmp(s->current_backoff_interval, max_backoff) > 0) {
      s->current_backoff_interval = max_backoff;
    }
  }

  gpr_mu_unlock(&s->mu);
}

const grpc_channel_args *grpc_client_setup_get_channel_args(
    grpc_client_setup_request *r) {
  return r->setup->args;
}

grpc_mdctx *grpc_client_setup_get_mdctx(grpc_client_setup_request *r) {
  return r->setup->mdctx;
}
