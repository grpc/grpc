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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <stdio.h>
#include <string.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

static const char oauth2_md[] = "Bearer aaslkfjs424535asdf";
static const char* client_identity_property_name = "smurf_name";
static const char* client_identity = "Brainy Smurf";

struct fullstack_secure_fixture_data {
  std::string localaddr;
  grpc_tls_version tls_version;
};

static const grpc_metadata* find_metadata(const grpc_metadata* md,
                                          size_t md_count, const char* key,
                                          const char* value) {
  size_t i;
  for (i = 0; i < md_count; i++) {
    if (grpc_slice_str_cmp(md[i].key, key) == 0 &&
        grpc_slice_str_cmp(md[i].value, value) == 0) {
      return &md[i];
    }
  }
  return nullptr;
}

typedef struct {
  size_t pseudo_refcount;
} test_processor_state;

static void process_oauth2_success(void* state, grpc_auth_context* ctx,
                                   const grpc_metadata* md, size_t md_count,
                                   grpc_process_auth_metadata_done_cb cb,
                                   void* user_data) {
  const grpc_metadata* oauth2 =
      find_metadata(md, md_count, "authorization", oauth2_md);
  test_processor_state* s;

  GPR_ASSERT(state != nullptr);
  s = static_cast<test_processor_state*>(state);
  GPR_ASSERT(s->pseudo_refcount == 1);
  GPR_ASSERT(oauth2 != nullptr);
  grpc_auth_context_add_cstring_property(ctx, client_identity_property_name,
                                         client_identity);
  GPR_ASSERT(grpc_auth_context_set_peer_identity_property_name(
                 ctx, client_identity_property_name) == 1);
  cb(user_data, oauth2, 1, nullptr, 0, GRPC_STATUS_OK, nullptr);
}

static void process_oauth2_failure(void* state, grpc_auth_context* /*ctx*/,
                                   const grpc_metadata* md, size_t md_count,
                                   grpc_process_auth_metadata_done_cb cb,
                                   void* user_data) {
  const grpc_metadata* oauth2 =
      find_metadata(md, md_count, "authorization", oauth2_md);
  test_processor_state* s;
  GPR_ASSERT(state != nullptr);
  s = static_cast<test_processor_state*>(state);
  GPR_ASSERT(s->pseudo_refcount == 1);
  GPR_ASSERT(oauth2 != nullptr);
  cb(user_data, oauth2, 1, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/,
    grpc_tls_version tls_version) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));
  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);
  ffd->tls_version = tls_version;
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
  return f;
}

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack_tls1_2(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  return chttp2_create_fixture_secure_fullstack(client_args, server_args,
                                                grpc_tls_version::TLS1_2);
}

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack_tls1_3(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  return chttp2_create_fixture_secure_fullstack(client_args, server_args,
                                                grpc_tls_version::TLS1_3);
}

static void chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args,
    grpc_channel_credentials* creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  f->client = grpc_secure_channel_create(creds, ffd->localaddr.c_str(),
                                         client_args, nullptr);
  GPR_ASSERT(f->client != nullptr);
  grpc_channel_credentials_release(creds);
}

static void chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args,
    grpc_server_credentials* server_creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  GPR_ASSERT(grpc_server_add_secure_http2_port(
      f->server, ffd->localaddr.c_str(), server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  delete ffd;
}

static void chttp2_init_client_simple_ssl_with_oauth2_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args) {
  grpc_core::ExecCtx exec_ctx;
  grpc_slice ca_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(CA_CERT_PATH, 1, &ca_slice)));
  const char* test_root_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(test_root_cert, nullptr, nullptr, nullptr);
  if (f != nullptr && ssl_creds != nullptr) {
    // Set the min and max TLS version.
    grpc_ssl_credentials* creds =
        reinterpret_cast<grpc_ssl_credentials*>(ssl_creds);
    fullstack_secure_fixture_data* ffd =
        static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
    creds->set_min_tls_version(ffd->tls_version);
    creds->set_max_tls_version(ffd->tls_version);
  }
  grpc_call_credentials* oauth2_creds = grpc_md_only_test_credentials_create(
      "authorization", oauth2_md, true /* is_async */);
  grpc_channel_credentials* ssl_oauth2_creds =
      grpc_composite_channel_credentials_create(ssl_creds, oauth2_creds,
                                                nullptr);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_oauth2_creds);
  grpc_channel_args_destroy(new_client_args);
  grpc_channel_credentials_release(ssl_creds);
  grpc_call_credentials_release(oauth2_creds);
  grpc_slice_unref(ca_slice);
}

static int fail_server_auth_check(grpc_channel_args* server_args) {
  size_t i;
  if (server_args == nullptr) return 0;
  for (i = 0; i < server_args->num_args; i++) {
    if (strcmp(server_args->args[i].key, FAIL_AUTH_CHECK_SERVER_ARG_NAME) ==
        0) {
      return 1;
    }
  }
  return 0;
}

static void processor_destroy(void* state) {
  test_processor_state* s = static_cast<test_processor_state*>(state);
  GPR_ASSERT((s->pseudo_refcount--) == 1);
  gpr_free(s);
}

static grpc_auth_metadata_processor test_processor_create(int failing) {
  test_processor_state* s =
      static_cast<test_processor_state*>(gpr_malloc(sizeof(*s)));
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
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args) {
  grpc_slice cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
  const char* server_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
  const char* server_key =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key, server_cert};
  grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
      nullptr, &pem_key_cert_pair, 1, 0, nullptr);
  if (f != nullptr && ssl_creds != nullptr) {
    // Set the min and max TLS version.
    grpc_ssl_server_credentials* creds =
        reinterpret_cast<grpc_ssl_server_credentials*>(ssl_creds);
    fullstack_secure_fixture_data* ffd =
        static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
    creds->set_min_tls_version(ffd->tls_version);
    creds->set_max_tls_version(ffd->tls_version);
  }
  grpc_server_credentials_set_auth_metadata_processor(
      ssl_creds, test_processor_create(fail_server_auth_check(server_args)));
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/simple_ssl_with_oauth2_fullstack_tls1_2",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack_tls1_2,
     chttp2_init_client_simple_ssl_with_oauth2_secure_fullstack,
     chttp2_init_server_simple_ssl_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
    {"chttp2/simple_ssl_with_oauth2_fullstack_tls1_3",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack_tls1_3,
     chttp2_init_client_simple_ssl_with_oauth2_secure_fullstack,
     chttp2_init_server_simple_ssl_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
};

int main(int argc, char** argv) {
  size_t i;
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
