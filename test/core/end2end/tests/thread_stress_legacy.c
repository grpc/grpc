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

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include "src/core/surface/event_string.h"
#include "src/core/surface/completion_queue.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/thd.h>
#include "test/core/util/test_config.h"

#define SERVER_THREADS 16
#define CLIENT_THREADS 16

static grpc_end2end_test_fixture g_fixture;
static gpr_timespec g_test_end_time;
static gpr_event g_client_done[CLIENT_THREADS];
static gpr_event g_server_done[SERVER_THREADS];
static gpr_mu g_mu;
static int g_active_requests;

static gpr_timespec n_seconds_time(int n) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

/* Drain pending events on a completion queue until it's ready to destroy.
   Does some post-processing to safely release memory on some of the events. */
static void drain_cq(int client, grpc_completion_queue *cq) {
  grpc_event *ev;
  grpc_completion_type type;
  char *evstr;
  int done = 0;
  char *name = client ? "client" : "server";
  while (!done) {
    ev = grpc_completion_queue_next(cq, five_seconds_time());
    if (!ev) {
      gpr_log(GPR_ERROR, "waiting for %s cq to drain", name);
      grpc_cq_dump_pending_ops(cq);
      continue;
    }

    evstr = grpc_event_string(ev);
    gpr_log(GPR_INFO, "got late %s event: %s", name, evstr);
    gpr_free(evstr);

    type = ev->type;
    switch (type) {
      case GRPC_SERVER_RPC_NEW:
        gpr_free(ev->tag);
        if (ev->call) {
          grpc_call_destroy(ev->call);
        }
        break;
      case GRPC_FINISHED:
        grpc_call_destroy(ev->call);
        break;
      case GRPC_QUEUE_SHUTDOWN:
        done = 1;
        break;
      case GRPC_READ:
      case GRPC_WRITE_ACCEPTED:
        if (!client && gpr_unref(ev->tag)) {
          gpr_free(ev->tag);
        }
      default:
        break;
    }
    grpc_event_finish(ev);
  }
}

/* Kick off a new request - assumes g_mu taken */
static void start_request(void) {
  gpr_slice slice = gpr_slice_malloc(100);
  grpc_byte_buffer *buf;
  grpc_call *call = grpc_channel_create_call_old(
      g_fixture.client, "/Foo", "foo.test.google.fr", g_test_end_time);

  memset(GPR_SLICE_START_PTR(slice), 1, GPR_SLICE_LENGTH(slice));
  buf = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);

  g_active_requests++;
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_invoke_old(call, g_fixture.client_cq, NULL, NULL, 0));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read_old(call, NULL));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_write_old(call, buf, NULL, 0));

  grpc_byte_buffer_destroy(buf);
}

/* Async client: handle sending requests, reading responses, and starting
   new requests when old ones finish */
static void client_thread(void *p) {
  gpr_intptr id = (gpr_intptr)p;
  grpc_event *ev;
  char *estr;

  for (;;) {
    ev = grpc_completion_queue_next(g_fixture.client_cq, n_seconds_time(1));
    if (ev) {
      switch (ev->type) {
        default:
          estr = grpc_event_string(ev);
          gpr_log(GPR_ERROR, "unexpected event: %s", estr);
          gpr_free(estr);
          break;
        case GRPC_READ:
          break;
        case GRPC_WRITE_ACCEPTED:
          GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done_old(ev->call, NULL));
          break;
        case GRPC_FINISH_ACCEPTED:
          break;
        case GRPC_CLIENT_METADATA_READ:
          break;
        case GRPC_FINISHED:
          /* kick off a new request if the test should still be running */
          gpr_mu_lock(&g_mu);
          g_active_requests--;
          if (gpr_time_cmp(gpr_now(), g_test_end_time) < 0) {
            start_request();
          }
          gpr_mu_unlock(&g_mu);
          grpc_call_destroy(ev->call);
          break;
      }
      grpc_event_finish(ev);
    }
    gpr_mu_lock(&g_mu);
    if (g_active_requests == 0) {
      gpr_mu_unlock(&g_mu);
      break;
    }
    gpr_mu_unlock(&g_mu);
  }

  gpr_event_set(&g_client_done[id], (void *)1);
}

/* Request a new server call. We tag them with a ref-count that starts at two,
   and decrements after each of: a read completes and a write completes.
   When it drops to zero, we write status */
static void request_server_call(void) {
  gpr_refcount *rc = gpr_malloc(sizeof(gpr_refcount));
  gpr_ref_init(rc, 2);
  grpc_server_request_call_old(g_fixture.server, rc);
}

static void maybe_end_server_call(grpc_call *call, gpr_refcount *rc) {
  if (gpr_unref(rc)) {
    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_write_status_old(
                                   call, GRPC_STATUS_OK, NULL, NULL));
    gpr_free(rc);
  }
}

static void server_thread(void *p) {
  int id = (gpr_intptr)p;
  gpr_slice slice = gpr_slice_malloc(100);
  grpc_byte_buffer *buf;
  grpc_event *ev;
  char *estr;

  memset(GPR_SLICE_START_PTR(slice), 1, GPR_SLICE_LENGTH(slice));
  buf = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);

  request_server_call();

  for (;;) {
    ev = grpc_completion_queue_next(g_fixture.server_cq, n_seconds_time(1));
    if (ev) {
      switch (ev->type) {
        default:
          estr = grpc_event_string(ev);
          gpr_log(GPR_ERROR, "unexpected event: %s", estr);
          gpr_free(estr);
          break;
        case GRPC_SERVER_RPC_NEW:
          if (ev->call) {
            GPR_ASSERT(GRPC_CALL_OK ==
                       grpc_call_server_accept_old(
                           ev->call, g_fixture.server_cq, ev->tag));
            GPR_ASSERT(GRPC_CALL_OK ==
                       grpc_call_server_end_initial_metadata_old(ev->call, 0));
            GPR_ASSERT(GRPC_CALL_OK ==
                       grpc_call_start_read_old(ev->call, ev->tag));
            GPR_ASSERT(GRPC_CALL_OK ==
                       grpc_call_start_write_old(ev->call, buf, ev->tag, 0));
          } else {
            gpr_free(ev->tag);
          }
          break;
        case GRPC_READ:
          if (ev->data.read) {
            GPR_ASSERT(GRPC_CALL_OK ==
                       grpc_call_start_read_old(ev->call, ev->tag));
          } else {
            maybe_end_server_call(ev->call, ev->tag);
          }
          break;
        case GRPC_WRITE_ACCEPTED:
          maybe_end_server_call(ev->call, ev->tag);
          break;
        case GRPC_FINISH_ACCEPTED:
          break;
        case GRPC_FINISHED:
          grpc_call_destroy(ev->call);
          request_server_call();
          break;
      }
      grpc_event_finish(ev);
    }
    gpr_mu_lock(&g_mu);
    if (g_active_requests == 0) {
      gpr_mu_unlock(&g_mu);
      break;
    }
    gpr_mu_unlock(&g_mu);
  }

  grpc_byte_buffer_destroy(buf);
  gpr_event_set(&g_server_done[id], (void *)1);
}

static void run_test(grpc_end2end_test_config config, int requests_in_flight) {
  int i;
  gpr_thd_id thd_id;

  gpr_log(GPR_INFO, "thread_stress_test/%s @ %d requests", config.name,
          requests_in_flight);

  /* setup client, server */
  g_fixture = config.create_fixture(NULL, NULL);
  config.init_client(&g_fixture, NULL);
  config.init_server(&g_fixture, NULL);

  /* schedule end time */
  g_test_end_time = n_seconds_time(5);

  g_active_requests = 0;
  gpr_mu_init(&g_mu);

  /* kick off threads */
  for (i = 0; i < CLIENT_THREADS; i++) {
    gpr_event_init(&g_client_done[i]);
    gpr_thd_new(&thd_id, client_thread, (void *)(gpr_intptr) i, NULL);
  }
  for (i = 0; i < SERVER_THREADS; i++) {
    gpr_event_init(&g_server_done[i]);
    gpr_thd_new(&thd_id, server_thread, (void *)(gpr_intptr) i, NULL);
  }

  /* start requests */
  gpr_mu_lock(&g_mu);
  for (i = 0; i < requests_in_flight; i++) {
    start_request();
  }
  gpr_mu_unlock(&g_mu);

  /* await completion */
  for (i = 0; i < CLIENT_THREADS; i++) {
    gpr_event_wait(&g_client_done[i], gpr_inf_future);
  }
  for (i = 0; i < SERVER_THREADS; i++) {
    gpr_event_wait(&g_server_done[i], gpr_inf_future);
  }

  /* shutdown the things */
  grpc_server_shutdown(g_fixture.server);
  grpc_server_destroy(g_fixture.server);
  grpc_channel_destroy(g_fixture.client);

  grpc_completion_queue_shutdown(g_fixture.server_cq);
  drain_cq(0, g_fixture.server_cq);
  grpc_completion_queue_destroy(g_fixture.server_cq);
  grpc_completion_queue_shutdown(g_fixture.client_cq);
  drain_cq(1, g_fixture.client_cq);
  grpc_completion_queue_destroy(g_fixture.client_cq);

  config.tear_down_data(&g_fixture);

  gpr_mu_destroy(&g_mu);
}

void grpc_end2end_tests(grpc_end2end_test_config config) {
  run_test(config, 1000);
}
