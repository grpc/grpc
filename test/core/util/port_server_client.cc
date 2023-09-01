//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "test/core/util/port_server_client.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/uri/uri_parser.h"

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
  grpc_http_request req;
  grpc_http_response rsp;
  freereq pr;
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

    std::string path = absl::StrFormat("/drop/%d", port);
    auto uri = grpc_core::URI::Create("https", GRPC_PORT_SERVER_ADDRESS, path,
                                      {} /* query params */, "" /* fragment */);
    GPR_ASSERT(uri.ok());
    auto http_request = grpc_core::HttpRequest::Get(
        std::move(*uri), nullptr /* channel args */, &pr.pops, &req,
        grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(30),
        GRPC_CLOSURE_CREATE(freed_port_from_server, &pr,
                            grpc_schedule_on_exec_ctx),
        &rsp,
        grpc_core::RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    http_request->Start();
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(pr.mu);
    while (!pr.done) {
      grpc_pollset_worker* worker = nullptr;
      if (!GRPC_LOG_IF_ERROR(
              "pollset_work",
              grpc_pollset_work(grpc_polling_entity_pollset(&pr.pops), &worker,
                                grpc_core::Timestamp::Now() +
                                    grpc_core::Duration::Seconds(1)))) {
        pr.done = 1;
      }
    }
    gpr_mu_unlock(pr.mu);

    grpc_pollset_shutdown(grpc_polling_entity_pollset(&pr.pops),
                          shutdown_closure);

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
  grpc_http_response response = {};
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request;
} portreq;

static void got_port_from_server(void* arg, grpc_error_handle error) {
  size_t i;
  int port = 0;
  portreq* pr = static_cast<portreq*>(arg);
  pr->http_request.reset();
  int failed = 0;
  grpc_http_response* response = &pr->response;

  if (!error.ok()) {
    failed = 1;
    gpr_log(GPR_DEBUG, "failed port pick from server: retrying [%s]",
            grpc_core::StatusToString(error).c_str());
  } else if (response->status != 200) {
    failed = 1;
    gpr_log(GPR_DEBUG, "failed port pick from server: status=%d",
            response->status);
  }

  if (failed) {
    grpc_http_request req;
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
    grpc_http_response_destroy(&pr->response);
    pr->response = {};
    auto uri = grpc_core::URI::Create("http", pr->server, "/get",
                                      {} /* query params */, "" /* fragment */);
    GPR_ASSERT(uri.ok());
    pr->http_request = grpc_core::HttpRequest::Get(
        std::move(*uri), nullptr /* channel args */, &pr->pops, &req,
        grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(30),
        GRPC_CLOSURE_CREATE(got_port_from_server, pr,
                            grpc_schedule_on_exec_ctx),
        &pr->response,
        grpc_core::RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    pr->http_request->Start();
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
  grpc_http_request req;
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
    auto uri = grpc_core::URI::Create("http", GRPC_PORT_SERVER_ADDRESS, "/get",
                                      {} /* query params */, "" /* fragment */);
    GPR_ASSERT(uri.ok());
    auto http_request = grpc_core::HttpRequest::Get(
        std::move(*uri), nullptr /* channel args */, &pr.pops, &req,
        grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(30),
        GRPC_CLOSURE_CREATE(got_port_from_server, &pr,
                            grpc_schedule_on_exec_ctx),
        &pr.response,
        grpc_core::RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    http_request->Start();
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(pr.mu);
    while (pr.port == -1) {
      grpc_pollset_worker* worker = nullptr;
      if (!GRPC_LOG_IF_ERROR(
              "pollset_work",
              grpc_pollset_work(grpc_polling_entity_pollset(&pr.pops), &worker,
                                grpc_core::Timestamp::Now() +
                                    grpc_core::Duration::Seconds(1)))) {
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
