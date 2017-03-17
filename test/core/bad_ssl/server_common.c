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

#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>
#include <signal.h>

#include "test/core/bad_ssl/server_common.h"
#include "test/core/util/test_config.h"

/* Common server implementation details for all servers in servers/.
 * There's nothing *wrong* with these servers per-se, but they are
 * configured to cause some failure case in the SSL connection path.
 */

static int got_sigint = 0;

static void sigint_handler(int x) { got_sigint = 1; }

const char *bad_ssl_addr(int argc, char **argv) {
  gpr_cmdline *cl;
  char *addr = NULL;
  cl = gpr_cmdline_create("test server");
  gpr_cmdline_add_string(cl, "bind", "Bind host:port", &addr);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);
  GPR_ASSERT(addr);
  return addr;
}

void bad_ssl_run(grpc_server *server) {
  int shutdown_started = 0;
  int shutdown_finished = 0;
  grpc_event ev;
  grpc_call_error error;
  grpc_call *s = NULL;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);

  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);

  grpc_server_register_completion_queue(server, cq, NULL);
  grpc_server_start(server);

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, (void *)1);
  GPR_ASSERT(GRPC_CALL_OK == error);

  signal(SIGINT, sigint_handler);
  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      gpr_log(GPR_INFO, "Shutting down due to SIGINT");
      grpc_server_shutdown_and_notify(server, cq, NULL);
      GPR_ASSERT(grpc_completion_queue_pluck(
                     cq, NULL, grpc_timeout_seconds_to_deadline(5), NULL)
                     .type == GRPC_OP_COMPLETE);
      grpc_completion_queue_shutdown(cq);
      shutdown_started = 1;
    }
    ev = grpc_completion_queue_next(
        cq, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(1000000, GPR_TIMESPAN)),
        NULL);
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        GPR_ASSERT(ev.tag == (void *)1);
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

  GPR_ASSERT(s == NULL);
  grpc_call_details_destroy(&call_details);
  grpc_metadata_array_destroy(&request_metadata_recv);
}
