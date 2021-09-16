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

#include <stdio.h>
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/support/string_ref.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

extern "C" {
#include <openssl/crypto.h>
}

static std::string test_server1_key_id;

namespace grpc {
namespace testing {

struct fullstack_secure_fixture_data {
  std::string localaddr;
};

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));

  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  return f;
}

static void process_auth_failure(void* state, grpc_auth_context* /*ctx*/,
                                 const grpc_metadata* /*md*/,
                                 size_t /*md_count*/,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void* user_data) {
  GPR_ASSERT(state == nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
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

#define SERVER_INIT_NAME(REQUEST_TYPE) \
  chttp2_init_server_simple_ssl_secure_fullstack_##REQUEST_TYPE

#define SERVER_INIT(REQUEST_TYPE)                                           \
  static void SERVER_INIT_NAME(REQUEST_TYPE)(                               \
      grpc_end2end_test_fixture * f, grpc_channel_args * server_args) {     \
    grpc_ssl_pem_key_cert_pair pem_cert_key_pair;                           \
    if (!test_server1_key_id.empty()) {                                     \
      pem_cert_key_pair.private_key = test_server1_key_id.c_str();          \
      pem_cert_key_pair.cert_chain = test_server1_cert;                     \
    } else {                                                                \
      pem_cert_key_pair.private_key = test_server1_key;                     \
      pem_cert_key_pair.cert_chain = test_server1_cert;                     \
    }                                                                       \
    grpc_server_credentials* ssl_creds =                                    \
        grpc_ssl_server_credentials_create_ex(                              \
            test_root_cert, &pem_cert_key_pair, 1, REQUEST_TYPE, NULL);     \
    if (fail_server_auth_check(server_args)) {                              \
      grpc_auth_metadata_processor processor = {process_auth_failure, NULL, \
                                                NULL};                      \
      grpc_server_credentials_set_auth_metadata_processor(ssl_creds,        \
                                                          processor);       \
    }                                                                       \
    chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);         \
  }

SERVER_INIT(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE)
SERVER_INIT(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY)
SERVER_INIT(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY)
SERVER_INIT(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY)
SERVER_INIT(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY)

#define CLIENT_INIT_NAME(cert_type) \
  chttp2_init_client_simple_ssl_secure_fullstack_##cert_type

typedef enum { NONE, SELF_SIGNED, SIGNED, BAD_CERT_PAIR } certtype;

#define CLIENT_INIT(cert_type)                                               \
  static void CLIENT_INIT_NAME(cert_type)(grpc_end2end_test_fixture * f,     \
                                          grpc_channel_args * client_args) { \
    grpc_channel_credentials* ssl_creds = NULL;                              \
    grpc_ssl_pem_key_cert_pair self_signed_client_key_cert_pair = {          \
        test_self_signed_client_key, test_self_signed_client_cert};          \
    grpc_ssl_pem_key_cert_pair signed_client_key_cert_pair = {               \
        test_signed_client_key, test_signed_client_cert};                    \
    grpc_ssl_pem_key_cert_pair bad_client_key_cert_pair = {                  \
        test_self_signed_client_key, test_signed_client_cert};               \
    grpc_ssl_pem_key_cert_pair* key_cert_pair = NULL;                        \
    switch (cert_type) {                                                     \
      case SELF_SIGNED:                                                      \
        key_cert_pair = &self_signed_client_key_cert_pair;                   \
        break;                                                               \
      case SIGNED:                                                           \
        key_cert_pair = &signed_client_key_cert_pair;                        \
        break;                                                               \
      case BAD_CERT_PAIR:                                                    \
        key_cert_pair = &bad_client_key_cert_pair;                           \
        break;                                                               \
      default:                                                               \
        break;                                                               \
    }                                                                        \
    ssl_creds = grpc_ssl_credentials_create(test_root_cert, key_cert_pair,   \
                                            NULL, NULL);                     \
    grpc_arg ssl_name_override = {                                           \
        GRPC_ARG_STRING,                                                     \
        const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),                \
        {const_cast<char*>("foo.test.google.fr")}};                          \
    grpc_channel_args* new_client_args =                                     \
        grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);  \
    chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);      \
    {                                                                        \
      grpc_core::ExecCtx exec_ctx;                                           \
      grpc_channel_args_destroy(new_client_args);                            \
    }                                                                        \
  }

CLIENT_INIT(NONE)
CLIENT_INIT(SELF_SIGNED)
CLIENT_INIT(SIGNED)
CLIENT_INIT(BAD_CERT_PAIR)

#define TEST_NAME(enum_name, cert_type, result) \
  "chttp2/ssl_" #enum_name "_" #cert_type "_" #result "_"

typedef enum { SUCCESS, FAIL } test_result;

#define SSL_TEST(request_type, cert_type, result)     \
  {                                                   \
    {TEST_NAME(request_type, cert_type, result),      \
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |       \
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS | \
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL,        \
     "foo.test.google.fr",                            \
     chttp2_create_fixture_secure_fullstack,          \
     CLIENT_INIT_NAME(cert_type),                     \
     SERVER_INIT_NAME(request_type),                  \
     chttp2_tear_down_secure_fullstack},              \
        result                                        \
  }

/* All test configurations */
typedef struct grpc_end2end_test_config_wrapper {
  grpc_end2end_test_config config;
  test_result result;
} grpc_end2end_test_config_wrapper;

static grpc_end2end_test_config_wrapper configs[] = {
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, NONE, SUCCESS),
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, SELF_SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, BAD_CERT_PAIR, FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, NONE,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, SELF_SIGNED,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, SIGNED,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY, BAD_CERT_PAIR,
             FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, NONE, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, SELF_SIGNED, FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, BAD_CERT_PAIR,
             FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             NONE, FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             SELF_SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             SIGNED, SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
             BAD_CERT_PAIR, FAIL),

    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY, NONE,
             FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
             SELF_SIGNED, FAIL),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY, SIGNED,
             SUCCESS),
    SSL_TEST(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
             BAD_CERT_PAIR, FAIL),
};

static void* tag(intptr_t t) { return (void*)t; }

static gpr_timespec n_seconds_time(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

// Shuts down the server.
// Side effect - Also shuts down and drains the completion queue.
static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_server_destroy(f->server);
  f->server = nullptr;
}

static void shutdown_client(grpc_end2end_test_fixture* f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = nullptr;
}

static void end_test(grpc_end2end_test_fixture* f) {
  shutdown_client(f);
  shutdown_server(f);
  grpc_completion_queue_destroy(f->cq);
}

static void simple_request_body(grpc_end2end_test_fixture f,
                                test_result expected_result) {
  grpc_call* c;
  gpr_timespec deadline = five_seconds_time();
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_call_error error;

  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr:1234");
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               deadline, nullptr);
  GPR_ASSERT(c);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), expected_result == SUCCESS);
  cq_verify(cqv);

  grpc_call_unref(c);
  cq_verifier_destroy(cqv);
}

class H2SslCertTest
    : public ::testing::TestWithParam<grpc_end2end_test_config_wrapper> {
 protected:
  H2SslCertTest() {
    gpr_log(GPR_INFO, "SSL_CERT_tests/%s", GetParam().config.name);
  }
  void SetUp() override {
    fixture_ = GetParam().config.create_fixture(nullptr, nullptr);
    GetParam().config.init_server(&fixture_, nullptr);
    GetParam().config.init_client(&fixture_, nullptr);
  }
  void TearDown() override {
    end_test(&fixture_);
    GetParam().config.tear_down_data(&fixture_);
  }

  grpc_end2end_test_fixture fixture_;
};

TEST_P(H2SslCertTest, SimpleRequestBody) {
  simple_request_body(fixture_, GetParam().result);
}

#ifndef OPENSSL_IS_BORINGSSL
#if GPR_LINUX
TEST_P(H2SslCertTest, SimpleRequestBodyUseEngine) {
  test_server1_key_id.clear();
  test_server1_key_id.append("engine:libengine_passthrough:");
  test_server1_key_id.append(test_server1_key);
  simple_request_body(fixture_, GetParam().result);
}
#endif
#endif

INSTANTIATE_TEST_SUITE_P(H2SslCert, H2SslCertTest,
                         ::testing::ValuesIn(configs));

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  FILE* roots_file;
  size_t roots_size = strlen(test_root_cert);
  char* roots_filename;

  grpc::testing::TestEnvironment env(argc, argv);
  /* Set the SSL roots env var. */
  roots_file =
      gpr_tmpfile("chttp2_simple_ssl_cert_fullstack_test", &roots_filename);
  GPR_ASSERT(roots_filename != nullptr);
  GPR_ASSERT(roots_file != nullptr);
  GPR_ASSERT(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, roots_filename);

  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();

  /* Cleanup. */
  remove(roots_filename);
  gpr_free(roots_filename);

  return ret;
}
