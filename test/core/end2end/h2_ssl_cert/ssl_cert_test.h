/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_H2SSLCERT_SSLCERTTEST_H
#define GRPC_TEST_CORE_END2END_H2SSLCERT_SSLCERTTEST_H

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/end2end_tests.h"

typedef enum { SUCCESS, FAIL } grpc_end2end_test_result;

typedef struct grpc_end2end_test_ssl_cert_test {
  grpc_end2end_test_config config;
  grpc_end2end_test_result result;
} grpc_end2end_test_ssl_cert_test;

extern grpc_end2end_test_ssl_cert_test configs[1];

grpc_end2end_test_fixture grpc_end2end_chttp2_create_fixture_secure_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args);
void grpc_end2end_chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *client_args,
    grpc_channel_credentials *creds);
void grpc_end2end_chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args,
    grpc_server_credentials *server_creds);
void grpc_end2end_chttp2_tear_down_secure_fullstack(
    grpc_end2end_test_fixture *f);
void grpc_end2end_process_auth_failure(void *state, grpc_auth_context *ctx,
                                       const grpc_metadata *md, size_t md_count,
                                       grpc_process_auth_metadata_done_cb cb,
                                       void *user_data);
int grpc_end2end_fail_server_auth_check(grpc_channel_args *server_args);

#define SERVER_INIT_NAME(REQUEST_TYPE) \
  chttp2_init_server_simple_ssl_secure_fullstack_##REQUEST_TYPE

#define SERVER_INIT(REQUEST_TYPE)                                       \
  static void SERVER_INIT_NAME(REQUEST_TYPE)(                           \
      grpc_end2end_test_fixture * f, grpc_channel_args * server_args) { \
    grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,   \
                                                    test_server1_cert}; \
    grpc_server_credentials *ssl_creds =                                \
        grpc_ssl_server_credentials_create_ex(                          \
            test_root_cert, &pem_cert_key_pair, 1, REQUEST_TYPE, NULL); \
    if (grpc_end2end_fail_server_auth_check(server_args)) {             \
      grpc_auth_metadata_processor processor = {                        \
          grpc_end2end_process_auth_failure, NULL, NULL};               \
      grpc_server_credentials_set_auth_metadata_processor(ssl_creds,    \
                                                          processor);   \
    }                                                                   \
    grpc_end2end_chttp2_init_server_secure_fullstack(f, server_args,    \
                                                     ssl_creds);        \
  }

#define CLIENT_INIT_NAME(cert_type) \
  chttp2_init_client_simple_ssl_secure_fullstack_##cert_type

typedef enum { NONE, SELF_SIGNED, SIGNED, BAD_CERT_PAIR } certtype;

#define CLIENT_INIT(cert_type)                                               \
  static void CLIENT_INIT_NAME(cert_type)(grpc_end2end_test_fixture * f,     \
                                          grpc_channel_args * client_args) { \
    grpc_channel_credentials *ssl_creds = NULL;                              \
    grpc_ssl_pem_key_cert_pair self_signed_client_key_cert_pair = {          \
        test_self_signed_client_key, test_self_signed_client_cert};          \
    grpc_ssl_pem_key_cert_pair signed_client_key_cert_pair = {               \
        test_signed_client_key, test_signed_client_cert};                    \
    grpc_ssl_pem_key_cert_pair bad_client_key_cert_pair = {                  \
        test_self_signed_client_key, test_signed_client_cert};               \
    grpc_ssl_pem_key_cert_pair *key_cert_pair = NULL;                        \
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
    ssl_creds =                                                              \
        grpc_ssl_credentials_create(test_root_cert, key_cert_pair, NULL);    \
    grpc_arg ssl_name_override = {GRPC_ARG_STRING,                           \
                                  GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,         \
                                  {"foo.test.google.fr"}};                   \
    grpc_channel_args *new_client_args =                                     \
        grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);  \
    grpc_end2end_chttp2_init_client_secure_fullstack(f, new_client_args,     \
                                                     ssl_creds);             \
    {                                                                        \
      grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;                           \
      grpc_channel_args_destroy(&exec_ctx, new_client_args);                 \
      grpc_exec_ctx_finish(&exec_ctx);                                       \
    }                                                                        \
  }

#define TEST_NAME(enum_name, cert_type, result) \
  "chttp2/ssl_" #enum_name "_" #cert_type "_" #result "_"

#define SSL_TEST(request_type, cert_type, result)                 \
  {                                                               \
    {TEST_NAME(request_type, cert_type, result),                  \
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |                   \
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |             \
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL,                    \
     grpc_end2end_chttp2_create_fixture_secure_fullstack,         \
     CLIENT_INIT_NAME(cert_type), SERVER_INIT_NAME(request_type), \
     grpc_end2end_chttp2_tear_down_secure_fullstack},             \
        result                                                    \
  }

#endif /* GRPC_TEST_CORE_END2END_H2SSLCERT_SSLCERTTEST_H */
