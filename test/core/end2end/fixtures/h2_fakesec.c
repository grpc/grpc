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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct fullstack_secure_fixture_data {
  char *localaddr;
} fullstack_secure_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data *ffd =
      gpr_malloc(sizeof(fullstack_secure_fixture_data));

  memset(&f, 0, sizeof(f));
  gpr_join_host_port(&ffd->localaddr, "localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create(NULL);

  return f;
}

static void process_auth_failure(void *state, grpc_auth_context *ctx,
                                 const grpc_metadata *md, size_t md_count,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void *user_data) {
  GPR_ASSERT(state == NULL);
  cb(user_data, NULL, 0, NULL, 0, GRPC_STATUS_UNAUTHENTICATED, NULL);
}

static void chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *client_args,
    grpc_channel_credentials *creds) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  f->client =
      grpc_secure_channel_create(creds, ffd->localaddr, client_args, NULL);
  GPR_ASSERT(f->client != NULL);
  grpc_channel_credentials_release(creds);
}

static void chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args,
    grpc_server_credentials *server_creds) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, NULL);
  grpc_server_register_completion_queue(f->server, f->cq, NULL);
  GPR_ASSERT(grpc_server_add_secure_http2_port(f->server, ffd->localaddr,
                                               server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture *f) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
}

static void chttp2_init_client_fake_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *client_args) {
  grpc_channel_credentials *fake_ts_creds =
      grpc_fake_transport_security_credentials_create();
  chttp2_init_client_secure_fullstack(f, client_args, fake_ts_creds);
}

static int fail_server_auth_check(grpc_channel_args *server_args) {
  size_t i;
  if (server_args == NULL) return 0;
  for (i = 0; i < server_args->num_args; i++) {
    if (strcmp(server_args->args[i].key, FAIL_AUTH_CHECK_SERVER_ARG_NAME) ==
        0) {
      return 1;
    }
  }
  return 0;
}

static void chttp2_init_server_fake_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args) {
  grpc_server_credentials *fake_ts_creds =
      grpc_fake_transport_security_server_credentials_create();
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, NULL, NULL};
    grpc_server_credentials_set_auth_metadata_processor(fake_ts_creds,
                                                        processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, fake_ts_creds);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/fake_secure_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_fake_secure_fullstack,
     chttp2_init_server_fake_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
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
