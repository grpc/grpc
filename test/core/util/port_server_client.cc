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

#include <grpc/support/port_platform.h>

#include "test/core/util/test_config.h"

#ifdef GRPC_TEST_PICK_PORT
#include <math.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/http/httpcli.h"
#include "test/core/util/port_server_client.h"

typedef struct freereq {
  gpr_mu* mu = nullptr;
  grpc_polling_entity pops = {};
  int done = 0;
} freereq;

static void destroy_pops_and_shutdown(void* p, grpc_error_handle /*error*/) {
  grpc_pollset* pollset =
      grpc_polling_entity_pollset(static_cast<grpc_polling_entity*>(p));
  grpc_pollset_destroy(pollset);
  gpr_free(pollset);
}

static void freed_port_from_server(void* arg, grpc_error_handle /*error*/) {
  freereq* pr = static_cast<freereq*>(arg);
  gpr_mu_lock(pr->mu);
  pr->done = 1;
  GRPC_LOG_IF_ERROR(
      "pollset_kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&pr->pops), nullptr));
  gpr_mu_unlock(pr->mu);
}

void grpc_free_port_using_server(int port) {
  grpc_httpcli_request req;
  grpc_httpcli_response rsp;
  freereq pr;
  char* path;
  grpc_closure* shutdown_closure;

  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;

    pr = {};
    memset(&req, 0, sizeof(req));
    rsp = {};

    grpc_pollset* pollset =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset, &pr.mu);
    pr.pops = grpc_polling_entity_create_from_pollset(pollset);
    shutdown_closure = GRPC_CLOSURE_CREATE(destroy_pops_and_shutdown, &pr.pops,
                                           grpc_schedule_on_exec_ctx);

    gpr_asprintf(&path, "/drop/%d", port);
    req.http.path = path;

    std::vector<grpc_arg> request_args;
    request_args.push_back(grpc_channel_arg_string_create(const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY), const_cast<char*>(GRPC_PORT_SERVER_ADDRESS)));
    grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, request_args.data(), request_args.size());
    auto httpcli = grpc_core::HttpCli::Get(
        args, &pr.pops, grpc_core::ResourceQuota::Default(), &req,
        absl::make_unique<
            grpc_core::HttpCli::PlaintextHttpCliHandshaker::Factory>(),
        grpc_core::ExecCtx::Get()->Now() + 30 * GPR_MS_PER_SEC,
        GRPC_CLOSURE_CREATE(freed_port_from_server, &pr,
                            grpc_schedule_on_exec_ctx),
        &rsp);
    httpcli->Start();
    grpc_channel_args_destroy(args);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(pr.mu);
    while (!pr.done) {
      grpc_pollset_worker* worker = nullptr;
      if (!GRPC_LOG_IF_ERROR(
              "pollset_work",
              grpc_pollset_work(
                  grpc_polling_entity_pollset(&pr.pops), &worker,
                  grpc_core::ExecCtx::Get()->Now() + GPR_MS_PER_SEC))) {
        pr.done = 1;
      }
    }
    gpr_mu_unlock(pr.mu);

    grpc_pollset_shutdown(grpc_polling_entity_pollset(&pr.pops),
                          shutdown_closure);

    gpr_free(path);
    grpc_http_response_destroy(&rsp);
  }
  grpc_shutdown();
}

typedef struct portreq {
  gpr_mu* mu = nullptr;
  grpc_polling_entity pops = {};
  int port = 0;
  int retries = 0;
  char* server = nullptr;
  grpc_httpcli_response response = {};
  grpc_core::OrphanablePtr<grpc_core::HttpCli> httpcli;
} portreq;

static void got_port_from_server(void* arg, grpc_error_handle error) {
  size_t i;
  int port = 0;
  portreq* pr = static_cast<portreq*>(arg);
  pr->httpcli.reset();
  int failed = 0;
  grpc_httpcli_response* response = &pr->response;

  if (error != GRPC_ERROR_NONE) {
    failed = 1;
    gpr_log(GPR_DEBUG, "failed port pick from server: retrying [%s]",
            grpc_error_std_string(error).c_str());
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
          grpc_pollset_kick(grpc_polling_entity_pollset(&pr->pops), nullptr));
      gpr_mu_unlock(pr->mu);
      return;
    }
    GPR_ASSERT(pr->retries < 10);
    gpr_sleep_until(gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_millis(
            static_cast<int64_t>(
                1000.0 * (1 + pow(1.3, pr->retries) * rand() / RAND_MAX)),
            GPR_TIMESPAN)));
    pr->retries++;
    req.http.path = const_cast<char*>("/get");
    grpc_http_response_destroy(&pr->response);
    pr->response = {};
    std::vector<grpc_arg> request_args;
    request_args.push_back(grpc_channel_arg_string_create(const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY), pr->server));
    grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, request_args.data(), request_args.size());
    pr->httpcli = grpc_core::HttpCli::Get(
        args, &pr->pops, grpc_core::ResourceQuota::Default(), &req,
        absl::make_unique<
            grpc_core::HttpCli::PlaintextHttpCliHandshaker::Factory>(),
        grpc_core::ExecCtx::Get()->Now() + 30 * GPR_MS_PER_SEC,
        GRPC_CLOSURE_CREATE(got_port_from_server, pr,
                            grpc_schedule_on_exec_ctx),
        &pr->response);
    pr->httpcli->Start();
    grpc_channel_args_destroy(args);
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
      grpc_pollset_kick(grpc_polling_entity_pollset(&pr->pops), nullptr));
  gpr_mu_unlock(pr->mu);
}

int grpc_pick_port_using_server(void) {
  grpc_httpcli_request req;
  portreq pr;
  grpc_closure* shutdown_closure;

  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    pr = {};
    memset(&req, 0, sizeof(req));
    grpc_pollset* pollset =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset, &pr.mu);
    pr.pops = grpc_polling_entity_create_from_pollset(pollset);
    shutdown_closure = GRPC_CLOSURE_CREATE(destroy_pops_and_shutdown, &pr.pops,
                                           grpc_schedule_on_exec_ctx);
    pr.port = -1;
    pr.server = const_cast<char*>(GRPC_PORT_SERVER_ADDRESS);

    req.http.path = const_cast<char*>("/get");

    std::vector<grpc_arg> request_args;
    request_args.push_back(grpc_channel_arg_string_create(const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY), const_cast<char*>(GRPC_PORT_SERVER_ADDRESS)));
    grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, request_args.data(), request_args.size());
    auto httpcli = grpc_core::HttpCli::Get(
        args, &pr.pops, grpc_core::ResourceQuota::Default(), &req,
        absl::make_unique<
            grpc_core::HttpCli::PlaintextHttpCliHandshaker::Factory>(),
        grpc_core::ExecCtx::Get()->Now() + 30 * GPR_MS_PER_SEC,
        GRPC_CLOSURE_CREATE(got_port_from_server, &pr,
                            grpc_schedule_on_exec_ctx),
        &pr.response);
    httpcli->Start();
    grpc_channel_args_destroy(args);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(pr.mu);
    while (pr.port == -1) {
      grpc_pollset_worker* worker = nullptr;
      if (!GRPC_LOG_IF_ERROR(
              "pollset_work",
              grpc_pollset_work(
                  grpc_polling_entity_pollset(&pr.pops), &worker,
                  grpc_core::ExecCtx::Get()->Now() + GPR_MS_PER_SEC))) {
        pr.port = 0;
      }
    }
    gpr_mu_unlock(pr.mu);

    grpc_http_response_destroy(&pr.response);
    grpc_pollset_shutdown(grpc_polling_entity_pollset(&pr.pops),
                          shutdown_closure);

    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_shutdown();

  return pr.port;
}

#endif  // GRPC_TEST_PICK_PORT
