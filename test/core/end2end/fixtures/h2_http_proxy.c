/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "src/core/ext/client_channel/client_channel.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/fixtures/http_proxy.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct fullstack_fixture_data {
  char *server_addr;
  grpc_end2end_http_proxy *proxy;
} fullstack_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  memset(&f, 0, sizeof(f));

  fullstack_fixture_data *ffd = gpr_malloc(sizeof(fullstack_fixture_data));
  const int server_port = grpc_pick_unused_port_or_die();
  gpr_join_host_port(&ffd->server_addr, "localhost", server_port);
  ffd->proxy = grpc_end2end_http_proxy_create();

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create(NULL);

  return f;
}

void chttp2_init_client_fullstack(grpc_end2end_test_fixture *f,
                                  grpc_channel_args *client_args) {
  fullstack_fixture_data *ffd = f->fixture_data;
  char *proxy_uri;
  gpr_asprintf(&proxy_uri, "http://%s",
               grpc_end2end_http_proxy_get_proxy_name(ffd->proxy));
  gpr_setenv("http_proxy", proxy_uri);
  gpr_free(proxy_uri);
  f->client = grpc_insecure_channel_create(ffd->server_addr, client_args, NULL);
  GPR_ASSERT(f->client);
}

void chttp2_init_server_fullstack(grpc_end2end_test_fixture *f,
                                  grpc_channel_args *server_args) {
  fullstack_fixture_data *ffd = f->fixture_data;
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, NULL);
  grpc_server_register_completion_queue(f->server, f->cq, NULL);
  GPR_ASSERT(grpc_server_add_insecure_http2_port(f->server, ffd->server_addr));
  grpc_server_start(f->server);
}

void chttp2_tear_down_fullstack(grpc_end2end_test_fixture *f) {
  fullstack_fixture_data *ffd = f->fixture_data;
  gpr_free(ffd->server_addr);
  grpc_end2end_http_proxy_destroy(ffd->proxy);
  gpr_free(ffd);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack", FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
                             FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                             FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     chttp2_create_fixture_fullstack, chttp2_init_client_fullstack,
     chttp2_init_server_fullstack, chttp2_tear_down_fullstack},
};

int main(int argc, char **argv) {
  size_t i;

  grpc_test_init(argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
