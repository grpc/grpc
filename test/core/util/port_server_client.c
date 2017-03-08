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
#include "test/core/util/test_config.h"

#ifdef GRPC_TEST_PICK_PORT
#include "test/core/util/port_server_client.h"

#include <math.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/http/httpcli.h"

typedef struct freereq {
  gpr_mu *mu;
  grpc_polling_entity pops;
  int done;
} freereq;

static void destroy_pops_and_shutdown(grpc_exec_ctx *exec_ctx, void *p,
                                      grpc_error *error) {
  grpc_pollset *pollset = grpc_polling_entity_pollset(p);
  grpc_pollset_destroy(pollset);
  gpr_free(pollset);
  grpc_shutdown();
}

static void freed_port_from_server(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_error *error) {
  freereq *pr = arg;
  gpr_mu_lock(pr->mu);
  pr->done = 1;
  GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&pr->pops), NULL));
  gpr_mu_unlock(pr->mu);
}

void grpc_free_port_using_server(int port) {
  grpc_httpcli_context context;
  grpc_httpcli_request req;
  grpc_httpcli_response rsp;
  freereq pr;
  char *path;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure *shutdown_closure;

  grpc_init();

  memset(&pr, 0, sizeof(pr));
  memset(&req, 0, sizeof(req));
  memset(&rsp, 0, sizeof(rsp));

  grpc_pollset *pollset = gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(pollset, &pr.mu);
  pr.pops = grpc_polling_entity_create_from_pollset(pollset);
  shutdown_closure = grpc_closure_create(destroy_pops_and_shutdown, &pr.pops,
                                         grpc_schedule_on_exec_ctx);

  req.host = GRPC_PORT_SERVER_ADDRESS;
  gpr_asprintf(&path, "/drop/%d", port);
  req.http.path = path;

  grpc_httpcli_context_init(&context);
  grpc_resource_quota *resource_quota =
      grpc_resource_quota_create("port_server_client/free");
  grpc_httpcli_get(&exec_ctx, &context, &pr.pops, resource_quota, &req,
                   grpc_timeout_seconds_to_deadline(10),
                   grpc_closure_create(freed_port_from_server, &pr,
                                       grpc_schedule_on_exec_ctx),
                   &rsp);
  grpc_resource_quota_unref_internal(&exec_ctx, resource_quota);
  gpr_mu_lock(pr.mu);
  while (!pr.done) {
    grpc_pollset_worker *worker = NULL;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(&exec_ctx, grpc_polling_entity_pollset(&pr.pops),
                              &worker, gpr_now(GPR_CLOCK_MONOTONIC),
                              grpc_timeout_seconds_to_deadline(1)))) {
      pr.done = 1;
    }
  }
  gpr_mu_unlock(pr.mu);

  grpc_httpcli_context_destroy(&exec_ctx, &context);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, grpc_polling_entity_pollset(&pr.pops),
                        shutdown_closure);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(path);
  grpc_http_response_destroy(&rsp);
}

typedef struct portreq {
  gpr_mu *mu;
  grpc_polling_entity pops;
  int port;
  int retries;
  char *server;
  grpc_httpcli_context *ctx;
  grpc_httpcli_response response;
} portreq;

static void got_port_from_server(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_error *error) {
  size_t i;
  int port = 0;
  portreq *pr = arg;
  int failed = 0;
  grpc_httpcli_response *response = &pr->response;

  if (error != GRPC_ERROR_NONE) {
    failed = 1;
    const char *msg = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "failed port pick from server: retrying [%s]", msg);

  } else if (response->status != 200) {
    failed = 1;
    gpr_log(GPR_DEBUG, "failed port pick from server: status=%d",
            response->status);
  }

  if (failed) {
    grpc_httpcli_request req;
    memset(&req, 0, sizeof(req));
    if (pr->retries >= 5) {
      gpr_mu_lock(pr->mu);
      pr->port = 0;
      GRPC_LOG_IF_ERROR(
          "pollset_kick",
          grpc_pollset_kick(grpc_polling_entity_pollset(&pr->pops), NULL));
      gpr_mu_unlock(pr->mu);
      return;
    }
    GPR_ASSERT(pr->retries < 10);
    gpr_sleep_until(gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_millis(
            (int64_t)(1000.0 * (1 + pow(1.3, pr->retries) * rand() / RAND_MAX)),
            GPR_TIMESPAN)));
    pr->retries++;
    req.host = pr->server;
    req.http.path = "/get";
    grpc_http_response_destroy(&pr->response);
    memset(&pr->response, 0, sizeof(pr->response));
    grpc_resource_quota *resource_quota =
        grpc_resource_quota_create("port_server_client/pick_retry");
    grpc_httpcli_get(exec_ctx, pr->ctx, &pr->pops, resource_quota, &req,
                     grpc_timeout_seconds_to_deadline(10),
                     grpc_closure_create(got_port_from_server, pr,
                                         grpc_schedule_on_exec_ctx),
                     &pr->response);
    grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
    return;
  }
  GPR_ASSERT(response);
  GPR_ASSERT(response->status == 200);
  for (i = 0; i < response->body_length; i++) {
    GPR_ASSERT(response->body[i] >= '0' && response->body[i] <= '9');
    port = port * 10 + response->body[i] - '0';
  }
  GPR_ASSERT(port > 1024);
  gpr_mu_lock(pr->mu);
  pr->port = port;
  GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&pr->pops), NULL));
  gpr_mu_unlock(pr->mu);
}

int grpc_pick_port_using_server(void) {
  grpc_httpcli_context context;
  grpc_httpcli_request req;
  portreq pr;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure *shutdown_closure;

  grpc_init();

  memset(&pr, 0, sizeof(pr));
  memset(&req, 0, sizeof(req));
  grpc_pollset *pollset = gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(pollset, &pr.mu);
  pr.pops = grpc_polling_entity_create_from_pollset(pollset);
  shutdown_closure = grpc_closure_create(destroy_pops_and_shutdown, &pr.pops,
                                         grpc_schedule_on_exec_ctx);
  pr.port = -1;
  pr.server = GRPC_PORT_SERVER_ADDRESS;
  pr.ctx = &context;

  req.host = GRPC_PORT_SERVER_ADDRESS;
  req.http.path = "/get";

  grpc_httpcli_context_init(&context);
  grpc_resource_quota *resource_quota =
      grpc_resource_quota_create("port_server_client/pick");
  grpc_httpcli_get(
      &exec_ctx, &context, &pr.pops, resource_quota, &req,
      grpc_timeout_seconds_to_deadline(10),
      grpc_closure_create(got_port_from_server, &pr, grpc_schedule_on_exec_ctx),
      &pr.response);
  grpc_resource_quota_unref_internal(&exec_ctx, resource_quota);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_mu_lock(pr.mu);
  while (pr.port == -1) {
    grpc_pollset_worker *worker = NULL;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(&exec_ctx, grpc_polling_entity_pollset(&pr.pops),
                              &worker, gpr_now(GPR_CLOCK_MONOTONIC),
                              grpc_timeout_seconds_to_deadline(1)))) {
      pr.port = 0;
    }
  }
  gpr_mu_unlock(pr.mu);

  grpc_http_response_destroy(&pr.response);
  grpc_httpcli_context_destroy(&exec_ctx, &context);
  grpc_pollset_shutdown(&exec_ctx, grpc_polling_entity_pollset(&pr.pops),
                        shutdown_closure);
  grpc_exec_ctx_finish(&exec_ctx);

  return pr.port;
}

#endif  // GRPC_TEST_PICK_PORT
