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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

void test_register_method_fail(void) {
  grpc_server *server = grpc_server_create(NULL, NULL);
  void *method;
  void *method_old;
  method =
      grpc_server_register_method(server, NULL, NULL, GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method == NULL);
  method_old =
      grpc_server_register_method(server, "m", "h", GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method_old != NULL);
  method = grpc_server_register_method(
      server, "m", "h", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER, 0);
  GPR_ASSERT(method == NULL);
  method_old =
      grpc_server_register_method(server, "m2", "h2", GRPC_SRM_PAYLOAD_NONE,
                                  GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
  GPR_ASSERT(method_old != NULL);
  method =
      grpc_server_register_method(server, "m2", "h2", GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method == NULL);
  method = grpc_server_register_method(
      server, "m2", "h2", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER,
      GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
  GPR_ASSERT(method == NULL);
  grpc_server_destroy(server);
}

void test_request_call_on_no_server_cq(void) {
  grpc_completion_queue *cc = grpc_completion_queue_create(NULL);
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE ==
             grpc_server_request_call(NULL, NULL, NULL, NULL, cc, cc, NULL));
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE ==
             grpc_server_request_registered_call(NULL, NULL, NULL, NULL, NULL,
                                                 NULL, cc, cc, NULL));
  grpc_completion_queue_destroy(cc);
}

void test_bind_server_twice(void) {
  char *addr;
  grpc_server *server1 = grpc_server_create(NULL, NULL);
  grpc_server *server2 = grpc_server_create(NULL, NULL);
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  int port = grpc_pick_unused_port_or_die();
  gpr_asprintf(&addr, "[::]:%d", port);
  grpc_server_register_completion_queue(server1, cq, NULL);
  grpc_server_register_completion_queue(server2, cq, NULL);
  GPR_ASSERT(port == grpc_server_add_insecure_http2_port(server1, addr));
  GPR_ASSERT(0 == grpc_server_add_insecure_http2_port(server2, addr));
  grpc_server_shutdown_and_notify(server1, cq, NULL);
  grpc_server_shutdown_and_notify(server2, cq, NULL);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL);
  grpc_server_destroy(server1);
  grpc_server_destroy(server2);
  grpc_completion_queue_destroy(cq);
  gpr_free(addr);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_register_method_fail();
  test_request_call_on_no_server_cq();
  test_bind_server_twice();
  grpc_shutdown();
  return 0;
}
