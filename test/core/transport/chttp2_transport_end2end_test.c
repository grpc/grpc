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

#include "transport_end2end_tests.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test/core/util/test_config.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/endpoint_pair.h"
#include "src/core/transport/chttp2_transport.h"
#include <grpc/support/log.h>

/* Wrapper to create an http2 transport pair */
static int create_http2_transport_for_test(
    grpc_transport_setup_callback client_setup_transport,
    void *client_setup_arg,
    grpc_transport_setup_callback server_setup_transport,
    void *server_setup_arg, size_t slice_size, grpc_mdctx *mdctx) {
  grpc_endpoint_pair p = grpc_iomgr_create_endpoint_pair(1);

  grpc_create_chttp2_transport(client_setup_transport, client_setup_arg, NULL,
                               p.client, NULL, 0, mdctx, 1);
  grpc_create_chttp2_transport(server_setup_transport, server_setup_arg, NULL,
                               p.server, NULL, 0, mdctx, 0);

  return 0;
}

static int create_http2_transport_for_test_small_slices(
    grpc_transport_setup_callback client_setup_transport,
    void *client_setup_arg,
    grpc_transport_setup_callback server_setup_transport,
    void *server_setup_arg, grpc_mdctx *mdctx) {
  return create_http2_transport_for_test(
      client_setup_transport, client_setup_arg, server_setup_transport,
      server_setup_arg, 1, mdctx);
}

static int create_http2_transport_for_test_medium_slices(
    grpc_transport_setup_callback client_setup_transport,
    void *client_setup_arg,
    grpc_transport_setup_callback server_setup_transport,
    void *server_setup_arg, grpc_mdctx *mdctx) {
  return create_http2_transport_for_test(
      client_setup_transport, client_setup_arg, server_setup_transport,
      server_setup_arg, 8192, mdctx);
}

static int create_http2_transport_for_test_large_slices(
    grpc_transport_setup_callback client_setup_transport,
    void *client_setup_arg,
    grpc_transport_setup_callback server_setup_transport,
    void *server_setup_arg, grpc_mdctx *mdctx) {
  return create_http2_transport_for_test(
      client_setup_transport, client_setup_arg, server_setup_transport,
      server_setup_arg, 1024 * 1024, mdctx);
}

/* All configurations to be tested */
grpc_transport_test_config fixture_configs[] = {
    {"chttp2_on_socketpair/small",
     create_http2_transport_for_test_small_slices},
    {"chttp2_on_socketpair/medium",
     create_http2_transport_for_test_medium_slices},
    {"chttp2_on_socketpair/large",
     create_http2_transport_for_test_large_slices},
};

/* Driver function: run the test suite for each test configuration */
int main(int argc, char **argv) {
  size_t i;

  grpc_test_init(argc, argv);
  grpc_iomgr_init();

  for (i = 0; i < sizeof(fixture_configs) / sizeof(*fixture_configs); i++) {
    grpc_transport_end2end_tests(&fixture_configs[i]);
  }

  grpc_iomgr_shutdown();

  gpr_log(GPR_INFO, "exiting");
  return 0;
}
