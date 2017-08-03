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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static const char oauth2_md[] = "Bearer aaslkfjs424535asdf";
static const char *client_identity_property_name = "smurf_name";
static const char *client_identity = "Brainy Smurf";

typedef struct fullstack_secure_fixture_data {
  char *localaddr;
} fullstack_secure_fixture_data;

static const grpc_metadata *find_metadata(const grpc_metadata *md,
                                          size_t md_count, const char *key,
                                          const char *value) {
  size_t i;
  for (i = 0; i < md_count; i++) {
    if (grpc_slice_str_cmp(md[i].key, key) == 0 &&
        grpc_slice_str_cmp(md[i].value, value) == 0) {
      return &md[i];
    }
  }
  return NULL;
}

typedef struct { size_t pseudo_refcount; } test_processor_state;

static void process_oauth2_success(void *state, grpc_auth_context *ctx,
                                   const grpc_metadata *md, size_t md_count,
                                   grpc_process_auth_metadata_done_cb cb,
                                   void *user_data) {
  const grpc_metadata *oauth2 =
      find_metadata(md, md_count, "authorization", oauth2_md);
  test_processor_state *s;

  GPR_ASSERT(state != NULL);
  s = (test_processor_state *)state;
  GPR_ASSERT(s->pseudo_refcount == 1);
  GPR_ASSERT(oauth2 != NULL);
  grpc_auth_context_add_cstring_property(ctx, client_identity_property_name,
                                         client_identity);
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                 ctx, client_identity_property_name) == 1);
  cb(user_data, oauth2, 1, NULL, 0, GRPC_STATUS_OK, NULL);
}

static void process_oauth2_failure(void *state, grpc_auth_context *ctx,
                                   const grpc_metadata *md, size_t md_count,
                                   grpc_process_auth_metadata_done_cb cb,
                                   void *user_data) {
  const grpc_metadata *oauth2 =
      find_metadata(md, md_count, "authorization", oauth2_md);
  test_processor_state *s;
  GPR_ASSERT(state != NULL);
  s = (test_processor_state *)state;
  GPR_ASSERT(s->pseudo_refcount == 1);
  GPR_ASSERT(oauth2 != NULL);
  cb(user_data, oauth2, 1, NULL, 0, GRPC_STATUS_UNAUTHENTICATED, NULL);
}

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data *ffd =
      gpr_malloc(sizeof(fullstack_secure_fixture_data));
  memset(&f, 0, sizeof(f));

  gpr_join_host_port(&ffd->localaddr, "localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(NULL);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(NULL);

  return f;
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

static void chttp2_init_client_simple_ssl_with_oauth2_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *client_args) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_channel_credentials *ssl_creds =
      grpc_ssl_credentials_create(test_root_cert, NULL, NULL);
  grpc_call_credentials *oauth2_creds = grpc_md_only_test_credentials_create(
      &exec_ctx, "authorization", oauth2_md, true /* is_async */);
  grpc_channel_credentials *ssl_oauth2_creds =
      grpc_composite_channel_credentials_create(ssl_creds, oauth2_creds, NULL);
  grpc_arg ssl_name_override = {GRPC_ARG_STRING,
                                GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                                {"foo.test.google.fr"}};
  grpc_channel_args *new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_oauth2_creds);
  grpc_channel_args_destroy(&exec_ctx, new_client_args);
  grpc_channel_credentials_release(ssl_creds);
  grpc_call_credentials_release(oauth2_creds);
  grpc_exec_ctx_finish(&exec_ctx);
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

static void processor_destroy(void *state) {
  test_processor_state *s = (test_processor_state *)state;
  GPR_ASSERT((s->pseudo_refcount--) == 1);
  gpr_free(s);
}

static grpc_auth_metadata_processor test_processor_create(int failing) {
  test_processor_state *s = gpr_malloc(sizeof(*s));
  grpc_auth_metadata_processor result;
  s->pseudo_refcount = 1;
  result.state = s;
  result.destroy = processor_destroy;
  if (failing) {
    result.process = process_oauth2_failure;
  } else {
    result.process = process_oauth2_success;
  }
  return result;
}

static void chttp2_init_server_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args) {
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials *ssl_creds =
      grpc_ssl_server_credentials_create(NULL, &pem_key_cert_pair, 1, 0, NULL);
  grpc_server_credentials_set_auth_metadata_processor(
      ssl_creds, test_processor_create(fail_server_auth_check(server_args)));
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/simple_ssl_with_oauth2_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_simple_ssl_with_oauth2_secure_fullstack,
     chttp2_init_server_simple_ssl_secure_fullstack,
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
