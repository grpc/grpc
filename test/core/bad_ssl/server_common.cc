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

#include <grpc/support/log.h>
#include <signal.h>

#include "test/core/bad_ssl/server_common.h"
#include "test/core/util/cmdline.h"
#include "test/core/util/test_config.h"

/* Common server implementation details for all servers in servers/.
 * There's nothing *wrong* with these servers per-se, but they are
 * configured to cause some failure case in the SSL connection path.
 */

static int got_sigint = 0;

static void sigint_handler(int /*x*/) { got_sigint = 1; }

const char* bad_ssl_addr(int argc, char** argv) {
  gpr_cmdline* cl;
  const char* addr = nullptr;
  cl = gpr_cmdline_create("test server");
  gpr_cmdline_add_string(cl, "bind", "Bind host:port", &addr);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);
  GPR_ASSERT(addr);
  return addr;
}

void bad_ssl_run(grpc_server* server) {
  int shutdown_started = 0;
  int shutdown_finished = 0;
  grpc_event ev;
  grpc_call_error error;
  grpc_call* s = nullptr;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_completion_queue* shutdown_cq;

  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);

  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_server_start(server);

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq,
                                   reinterpret_cast<void*>(1));
  GPR_ASSERT(GRPC_CALL_OK == error);

  signal(SIGINT, sigint_handler);
  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      gpr_log(GPR_INFO, "Shutting down due to SIGINT");
      shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
      grpc_server_shutdown_and_notify(server, shutdown_cq, nullptr);
      GPR_ASSERT(grpc_completion_queue_pluck(
                     shutdown_cq, nullptr, grpc_timeout_seconds_to_deadline(5),
                     nullptr)
                     .type == GRPC_OP_COMPLETE);
      grpc_completion_queue_destroy(shutdown_cq);
      grpc_completion_queue_shutdown(cq);
      shutdown_started = 1;
    }
    ev = grpc_completion_queue_next(
        cq,
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_micros(1000000, GPR_TIMESPAN)),
        nullptr);
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        GPR_ASSERT(ev.tag == (void*)1);
        GPR_ASSERT(ev.success == 0);
        break;
      case GRPC_QUEUE_SHUTDOWN:
        GPR_ASSERT(shutdown_started);
        shutdown_finished = 1;
        break;
      case GRPC_QUEUE_TIMEOUT:
        break;
    }
  }

  GPR_ASSERT(s == nullptr);
  grpc_call_details_destroy(&call_details);
  grpc_metadata_array_destroy(&request_metadata_recv);
}
