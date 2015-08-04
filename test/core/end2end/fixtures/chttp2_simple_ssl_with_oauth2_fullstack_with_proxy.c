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

#include "src/core/channel/channel_args.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/security/credentials.h"
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/util/test_config.h"
#include "test/core/util/port.h"

typedef struct fullstack_secure_fixture_data {
  grpc_end2end_proxy *proxy;
} fullstack_secure_fixture_data;

static grpc_server *create_proxy_server(const char *port) {
  grpc_server *s = grpc_server_create(NULL);
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials *ssl_creds =
      grpc_ssl_server_credentials_create(NULL, &pem_cert_key_pair, 1, 0);
  GPR_ASSERT(grpc_server_add_secure_http2_port(s, port, ssl_creds));
  grpc_server_credentials_release(ssl_creds);
  return s;
}

static grpc_channel *create_proxy_client(const char *target) {
  grpc_channel *channel;
  grpc_credentials *ssl_creds = grpc_ssl_credentials_create(NULL, NULL);
  grpc_arg ssl_name_override = {GRPC_ARG_STRING,
                                GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                                {"foo.test.google.fr"}};
  grpc_channel_args client_args;
  client_args.num_args = 1;
  client_args.args = &ssl_name_override;
  channel = grpc_secure_channel_create(ssl_creds, target, &client_args);
  grpc_credentials_release(ssl_creds);
  return channel;
}

static const grpc_end2end_proxy_def proxy_def = {create_proxy_server,
                                                 create_proxy_client};

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  fullstack_secure_fixture_data *ffd =
      gpr_malloc(sizeof(fullstack_secure_fixture_data));
  memset(&f, 0, sizeof(f));

  ffd->proxy = grpc_end2end_proxy_create(&proxy_def);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create();

  return f;
}

static void chttp2_init_client_secure_fullstack(grpc_end2end_test_fixture *f,
                                                grpc_channel_args *client_args,
                                                grpc_credentials *creds) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  f->client = grpc_secure_channel_create(
      creds, grpc_end2end_proxy_get_client_target(ffd->proxy), client_args);
  GPR_ASSERT(f->client != NULL);
  grpc_credentials_release(creds);
}

static void chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args,
    grpc_server_credentials *server_creds) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args);
  grpc_server_register_completion_queue(f->server, f->cq);
  GPR_ASSERT(grpc_server_add_secure_http2_port(
      f->server, grpc_end2end_proxy_get_server_port(ffd->proxy), server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture *f) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  grpc_end2end_proxy_destroy(ffd->proxy);
  gpr_free(ffd);
}

static void chttp2_init_client_simple_ssl_with_oauth2_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *client_args) {
  grpc_credentials *ssl_creds =
      grpc_ssl_credentials_create(test_root_cert, NULL);
  grpc_credentials *oauth2_creds =
      grpc_fake_oauth2_credentials_create("Bearer aaslkfjs424535asdf", 1);
  grpc_credentials *ssl_oauth2_creds =
      grpc_composite_credentials_create(ssl_creds, oauth2_creds);
  grpc_arg ssl_name_override = {GRPC_ARG_STRING,
                                GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                                {"foo.test.google.fr"}};
  grpc_channel_args *new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_oauth2_creds);
  grpc_channel_args_destroy(new_client_args);
  grpc_credentials_release(ssl_creds);
  grpc_credentials_release(oauth2_creds);
}

static void chttp2_init_server_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args) {
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials *ssl_creds =
      grpc_ssl_server_credentials_create(NULL, &pem_key_cert_pair, 1, 0);
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/simple_ssl_with_oauth2_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_HOSTNAME_VERIFICATION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS,
     chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_simple_ssl_with_oauth2_secure_fullstack,
     chttp2_init_server_simple_ssl_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
};

int main(int argc, char **argv) {
  size_t i;
  grpc_test_init(argc, argv);

  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(configs[i]);
  }

  grpc_shutdown();

  return 0;
}
