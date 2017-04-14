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
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/tsi/fake_transport_security.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

void test_unparsable_target(void) {
  grpc_channel_args args = {0, NULL};
  grpc_server *server = grpc_server_create(&args, NULL);
  int port = grpc_server_add_insecure_http2_port(server, "[");
  GPR_ASSERT(port == 0);
  grpc_server_destroy(server);
}

void test_add_same_port_twice() {
  grpc_arg a;
  a.type = GRPC_ARG_INTEGER;
  a.key = GRPC_ARG_ALLOW_REUSEPORT;
  a.value.integer = 0;
  grpc_channel_args args = {1, &a};

  int port = grpc_pick_unused_port_or_die();
  char *addr = NULL;
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  grpc_server *server = grpc_server_create(&args, NULL);
  grpc_server_credentials *fake_creds =
      grpc_fake_transport_security_server_credentials_create();
  gpr_join_host_port(&addr, "localhost", port);
  GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, fake_creds));
  GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, fake_creds) == 0);

  grpc_server_credentials_release(fake_creds);
  gpr_free(addr);
  grpc_server_shutdown_and_notify(server, cq, NULL);
  grpc_completion_queue_pluck(cq, NULL, gpr_inf_future(GPR_CLOCK_REALTIME),
                              NULL);
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_unparsable_target();
  test_add_same_port_twice();
  grpc_shutdown();
  return 0;
}
