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

#include "src/core/lib/http/httpcli.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/subprocess.h>
#include <grpc/support/sync.h>
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static int g_done = 0;
static grpc_httpcli_context g_context;
static gpr_mu* g_mu;
static grpc_polling_entity g_pops;

static grpc_millis n_seconds_time(int seconds) {
  return grpc_timespec_to_millis_round_up(
      grpc_timeout_seconds_to_deadline(seconds));
}

static void on_finish(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
  const char* expect =
      "<html><head><title>Hello world!</title></head>"
      "<body><p>This is a test</p></body></html>";
  grpc_http_response* response = static_cast<grpc_http_response*>(arg);
  GPR_ASSERT(response);
  GPR_ASSERT(response->status == 200);
  GPR_ASSERT(response->body_length == strlen(expect));
  GPR_ASSERT(0 == memcmp(expect, response->body, response->body_length));
  gpr_mu_lock(g_mu);
  g_done = 1;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(exec_ctx, grpc_polling_entity_pollset(&g_pops),
                        nullptr)));
  gpr_mu_unlock(g_mu);
}

static void test_get(int port) {
  grpc_httpcli_request req;
  char* host;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  g_done = 0;
  gpr_log(GPR_INFO, "test_get");

  gpr_asprintf(&host, "localhost:%d", port);
  gpr_log(GPR_INFO, "requesting from %s", host);

  memset(&req, 0, sizeof(req));
  req.host = host;
  req.http.path = const_cast<char*>("/get");
  req.handshaker = &grpc_httpcli_plaintext;

  grpc_http_response response;
  memset(&response, 0, sizeof(response));
  grpc_resource_quota* resource_quota = grpc_resource_quota_create("test_get");
  grpc_httpcli_get(
      &exec_ctx, &g_context, &g_pops, resource_quota, &req, n_seconds_time(15),
      GRPC_CLOSURE_CREATE(on_finish, &response, grpc_schedule_on_exec_ctx),
      &response);
  grpc_resource_quota_unref_internal(&exec_ctx, resource_quota);
  gpr_mu_lock(g_mu);
  while (!g_done) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(&exec_ctx, grpc_polling_entity_pollset(&g_pops),
                          &worker, n_seconds_time(1))));
    gpr_mu_unlock(g_mu);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_mu_lock(g_mu);
  }
  gpr_mu_unlock(g_mu);
  gpr_free(host);
  grpc_http_response_destroy(&response);
}

static void test_post(int port) {
  grpc_httpcli_request req;
  char* host;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  g_done = 0;
  gpr_log(GPR_INFO, "test_post");

  gpr_asprintf(&host, "localhost:%d", port);
  gpr_log(GPR_INFO, "posting to %s", host);

  memset(&req, 0, sizeof(req));
  req.host = host;
  req.http.path = const_cast<char*>("/post");
  req.handshaker = &grpc_httpcli_plaintext;

  grpc_http_response response;
  memset(&response, 0, sizeof(response));
  grpc_resource_quota* resource_quota = grpc_resource_quota_create("test_post");
  grpc_httpcli_post(
      &exec_ctx, &g_context, &g_pops, resource_quota, &req, "hello", 5,
      n_seconds_time(15),
      GRPC_CLOSURE_CREATE(on_finish, &response, grpc_schedule_on_exec_ctx),
      &response);
  grpc_resource_quota_unref_internal(&exec_ctx, resource_quota);
  gpr_mu_lock(g_mu);
  while (!g_done) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(&exec_ctx, grpc_polling_entity_pollset(&g_pops),
                          &worker, n_seconds_time(1))));
    gpr_mu_unlock(g_mu);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_mu_lock(g_mu);
  }
  gpr_mu_unlock(g_mu);
  gpr_free(host);
  grpc_http_response_destroy(&response);
}

static void destroy_pops(grpc_exec_ctx* exec_ctx, void* p, grpc_error* error) {
  grpc_pollset_destroy(exec_ctx, grpc_polling_entity_pollset(
                                     static_cast<grpc_polling_entity*>(p)));
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_subprocess* server;
  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char* args[4];
  int port = grpc_pick_unused_port_or_die();
  int arg_shift = 0;
  /* figure out where we are */
  char* root;
  if (lslash) {
    root = static_cast<char*>(gpr_malloc((size_t)(lslash - me + 1)));
    memcpy(root, me, (size_t)(lslash - me));
    root[lslash - me] = 0;
  } else {
    root = gpr_strdup(".");
  }

  GPR_ASSERT(argc <= 2);
  if (argc == 2) {
    args[0] = gpr_strdup(argv[1]);
  } else {
    arg_shift = 1;
    gpr_asprintf(&args[0], "%s/../../tools/distrib/python_wrapper.sh", root);
    gpr_asprintf(&args[1], "%s/../../test/core/http/test_server.py", root);
  }

  /* start the server */
  args[1 + arg_shift] = const_cast<char*>("--port");
  gpr_asprintf(&args[2 + arg_shift], "%d", port);
  server = gpr_subprocess_create(3 + arg_shift, (const char**)args);
  GPR_ASSERT(server);
  gpr_free(args[0]);
  if (arg_shift) gpr_free(args[1]);
  gpr_free(args[2 + arg_shift]);
  gpr_free(root);

  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(5, GPR_TIMESPAN)));

  grpc_test_init(argc, argv);
  grpc_init();
  grpc_httpcli_context_init(&g_context);
  grpc_pollset* pollset =
      static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(pollset, &g_mu);
  g_pops = grpc_polling_entity_create_from_pollset(pollset);

  test_get(port);
  test_post(port);

  grpc_httpcli_context_destroy(&exec_ctx, &g_context);
  GRPC_CLOSURE_INIT(&destroyed, destroy_pops, &g_pops,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, grpc_polling_entity_pollset(&g_pops),
                        &destroyed);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();

  gpr_free(grpc_polling_entity_pollset(&g_pops));

  gpr_subprocess_destroy(server);

  return 0;
}
