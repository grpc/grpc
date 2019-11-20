/*
 *
 * Copyright 2019 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

#include <openssl/rsa.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/composite/composite_credentials.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/data/ssl_test_data.h"

namespace {

grpc_completion_queue* g_cq;
grpc_server* g_server;
int g_port;

void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(
        cq, grpc_timeout_milliseconds_to_deadline(5000), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

void* tag(int i) { return (void*)static_cast<intptr_t>(i); }

grpc_channel_credentials* create_test_ssl_plus_token_channel_creds(
    const char* token) {
  grpc_channel_credentials* channel_creds =
      grpc_ssl_credentials_create(test_root_cert, nullptr, nullptr, nullptr);
  grpc_call_credentials* call_creds =
      grpc_access_token_credentials_create(token, nullptr);
  grpc_channel_credentials* composite_creds =
      grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(call_creds);
  return composite_creds;
}

grpc_server_credentials* create_test_ssl_server_creds() {
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  return grpc_ssl_server_credentials_create_ex(
      test_root_cert, &pem_cert_key_pair, 1,
      GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, nullptr);
}

// Perform a simple RPC and capture the ASCII value of the
// authorization metadata sent to the server, if any. Return
// nullptr if no authorization metadata was sent to the server.
grpc_core::UniquePtr<char> perform_call_and_get_authorization_header(
    grpc_channel_credentials* channel_creds) {
  // Create a new channel and call
  grpc_core::UniquePtr<char> server_addr = nullptr;
  grpc_core::JoinHostPort(&server_addr, "localhost", g_port);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* channel_args =
      grpc_channel_args_copy_and_add(nullptr, &ssl_name_override, 1);
  grpc_channel* channel = grpc_secure_channel_create(
      channel_creds, server_addr.get(), channel_args, nullptr);
  grpc_channel_args_destroy(channel_args);
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(g_cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  grpc_slice request_payload_slice = grpc_slice_from_copied_string("request");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice response_payload_slice = grpc_slice_from_copied_string("response");
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload_recv = nullptr;
  // Start a call
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, g_cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  // Request a call on the server
  error =
      grpc_server_request_call(g_server, &s, &call_details,
                               &request_metadata_recv, g_cq, g_cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op->flags = 0;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  // Extract the ascii value of the authorization header, if present
  grpc_core::UniquePtr<char> authorization_header_val;
  gpr_log(GPR_DEBUG, "RPC done. Now examine received metadata on server...");
  for (size_t i = 0; i < request_metadata_recv.count; i++) {
    char* cur_key =
        grpc_dump_slice(request_metadata_recv.metadata[i].key, GPR_DUMP_ASCII);
    char* cur_val = grpc_dump_slice(request_metadata_recv.metadata[i].value,
                                    GPR_DUMP_ASCII);
    gpr_log(GPR_DEBUG, "key[%" PRIdPTR "]=%s val[%" PRIdPTR "]=%s", i, cur_key,
            i, cur_val);
    if (gpr_stricmp(cur_key, "authorization") == 0) {
      // This test is broken if we found multiple authorization headers.
      GPR_ASSERT(authorization_header_val == nullptr);
      authorization_header_val.reset(gpr_strdup(cur_val));
      gpr_log(GPR_DEBUG, "Found authorization header: %s",
              authorization_header_val.get());
    }
    gpr_free(cur_key);
    gpr_free(cur_val);
  }
  // cleanup
  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);
  grpc_call_unref(c);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
  grpc_channel_destroy(channel);
  return authorization_header_val;
}

void test_attach_and_get() {
  grpc_channel_credentials* main_creds =
      create_test_ssl_plus_token_channel_creds("main-auth-header");
  grpc_channel_credentials* foo_creds =
      create_test_ssl_plus_token_channel_creds("foo-auth-header");
  grpc_channel_credentials* bar_creds =
      create_test_ssl_plus_token_channel_creds("bar-auth-header");
  auto foo_key = grpc_core::UniquePtr<char>(gpr_strdup("foo"));
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(
                 main_creds, foo_key.get(), foo_creds) == true);
  auto bar_key = grpc_core::UniquePtr<char>(gpr_strdup("bar"));
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(
                 main_creds, bar_key.get(), bar_creds) == true);
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(main_creds, "foo",
                                                         foo_creds) == false);
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(main_creds, "bar",
                                                         bar_creds) == false);
  grpc_channel_credentials_release(foo_creds);
  grpc_channel_credentials_release(bar_creds);
  {
    // Creds that send auth header with value "foo-auth-header" are attached on
    // main creds under key "foo"
    auto foo_auth_header = perform_call_and_get_authorization_header(
        main_creds->get_control_plane_credentials("foo").get());
    GPR_ASSERT(foo_auth_header != nullptr &&
               gpr_stricmp(foo_auth_header.get(), "Bearer foo-auth-header") ==
                   0);
  }
  {
    // Creds that send auth header with value "bar-auth-header" are attached on
    // main creds under key "bar"
    auto bar_auth_header = perform_call_and_get_authorization_header(
        main_creds->get_control_plane_credentials("bar").get());
    GPR_ASSERT(bar_auth_header != nullptr &&
               gpr_stricmp(bar_auth_header.get(), "Bearer bar-auth-header") ==
                   0);
  }
  {
    // Sanity check that the main creds themselves send an authorization header
    // with value "main".
    auto main_auth_header =
        perform_call_and_get_authorization_header(main_creds);
    GPR_ASSERT(main_auth_header != nullptr &&
               gpr_stricmp(main_auth_header.get(), "Bearer main-auth-header") ==
                   0);
  }
  {
    // If a key isn't mapped in the per channel or global registries, then the
    // credentials should be returned but with their per-call creds stripped.
    // The end effect is that we shouldn't see any authorization metadata
    // sent from client to server.
    auto unmapped_auth_header = perform_call_and_get_authorization_header(
        main_creds->get_control_plane_credentials("unmapped").get());
    GPR_ASSERT(unmapped_auth_header == nullptr);
  }
  grpc_channel_credentials_release(main_creds);
}

void test_registering_same_creds_under_different_keys() {
  grpc_channel_credentials* main_creds =
      create_test_ssl_plus_token_channel_creds("main-auth-header");
  grpc_channel_credentials* foo_creds =
      create_test_ssl_plus_token_channel_creds("foo-auth-header");
  auto foo_key = grpc_core::UniquePtr<char>(gpr_strdup("foo"));
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(
                 main_creds, foo_key.get(), foo_creds) == true);
  auto foo2_key = grpc_core::UniquePtr<char>(gpr_strdup("foo2"));
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(
                 main_creds, foo2_key.get(), foo_creds) == true);
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(main_creds, "foo",
                                                         foo_creds) == false);
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(main_creds, "foo2",
                                                         foo_creds) == false);
  grpc_channel_credentials_release(foo_creds);
  {
    // Access foo creds via foo
    auto foo_auth_header = perform_call_and_get_authorization_header(
        main_creds->get_control_plane_credentials("foo").get());
    GPR_ASSERT(foo_auth_header != nullptr &&
               gpr_stricmp(foo_auth_header.get(), "Bearer foo-auth-header") ==
                   0);
  }
  {
    // Access foo creds via foo2
    auto foo_auth_header = perform_call_and_get_authorization_header(
        main_creds->get_control_plane_credentials("foo2").get());
    GPR_ASSERT(foo_auth_header != nullptr &&
               gpr_stricmp(foo_auth_header.get(), "Bearer foo-auth-header") ==
                   0);
  }
  grpc_channel_credentials_release(main_creds);
}

// Note that this test uses control plane creds registered in the global
// map. This global registration is done before this and any other
// test is invoked.
void test_attach_and_get_with_global_registry() {
  grpc_channel_credentials* main_creds =
      create_test_ssl_plus_token_channel_creds("main-auth-header");
  grpc_channel_credentials* global_override_creds =
      create_test_ssl_plus_token_channel_creds("global-override-auth-header");
  grpc_channel_credentials* random_creds =
      create_test_ssl_plus_token_channel_creds("random-auth-header");
  auto global_key = grpc_core::UniquePtr<char>(gpr_strdup("global"));
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(
                 main_creds, global_key.get(), global_override_creds) == true);
  GPR_ASSERT(grpc_channel_credentials_attach_credentials(
                 main_creds, "global", global_override_creds) == false);
  grpc_channel_credentials_release(global_override_creds);
  {
    // The global registry should be used if a key isn't registered on the per
    // channel registry
    auto global_auth_header = perform_call_and_get_authorization_header(
        random_creds->get_control_plane_credentials("global").get());
    GPR_ASSERT(global_auth_header != nullptr &&
               gpr_stricmp(global_auth_header.get(),
                           "Bearer global-auth-header") == 0);
  }
  {
    // The per-channel registry should be preferred over the global registry
    auto override_auth_header = perform_call_and_get_authorization_header(
        main_creds->get_control_plane_credentials("global").get());
    GPR_ASSERT(override_auth_header != nullptr &&
               gpr_stricmp(override_auth_header.get(),
                           "Bearer global-override-auth-header") == 0);
  }
  {
    // Sanity check that random creds themselves send authorization header with
    // value "random".
    auto random_auth_header =
        perform_call_and_get_authorization_header(random_creds);
    GPR_ASSERT(random_auth_header != nullptr &&
               gpr_stricmp(random_auth_header.get(),
                           "Bearer random-auth-header") == 0);
  }
  {
    // If a key isn't mapped in the per channel or global registries, then the
    // credentials should be returned but with their per-call creds stripped.
    // The end effect is that we shouldn't see any authorization metadata
    // sent from client to server.
    auto unmapped_auth_header = perform_call_and_get_authorization_header(
        random_creds->get_control_plane_credentials("unmapped").get());
    GPR_ASSERT(unmapped_auth_header == nullptr);
  }
  grpc_channel_credentials_release(main_creds);
  grpc_channel_credentials_release(random_creds);
}

}  // namespace

int main(int argc, char** argv) {
  {
    grpc::testing::TestEnvironment env(argc, argv);
    grpc_init();
    // First setup a global server for all tests to use
    g_cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_server_credentials* server_creds = create_test_ssl_server_creds();
    g_server = grpc_server_create(nullptr, nullptr);
    g_port = grpc_pick_unused_port_or_die();
    grpc_server_register_completion_queue(g_server, g_cq, nullptr);
    grpc_core::UniquePtr<char> localaddr;
    grpc_core::JoinHostPort(&localaddr, "localhost", g_port);
    GPR_ASSERT(grpc_server_add_secure_http2_port(g_server, localaddr.get(),
                                                 server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(g_server);
    {
      // First, Register one channel creds in the global registry; all tests
      // will have access.
      grpc_channel_credentials* global_creds =
          create_test_ssl_plus_token_channel_creds("global-auth-header");
      auto global_key = grpc_core::UniquePtr<char>(gpr_strdup("global"));
      GPR_ASSERT(grpc_control_plane_credentials_register(global_key.get(),
                                                         global_creds) == true);
      GPR_ASSERT(grpc_control_plane_credentials_register(
                     "global", global_creds) == false);
      grpc_channel_credentials_release(global_creds);
    }
    // Run tests
    {
      test_attach_and_get();
      test_registering_same_creds_under_different_keys();
      test_attach_and_get_with_global_registry();
    }
    // cleanup
    grpc_completion_queue* shutdown_cq =
        grpc_completion_queue_create_for_pluck(nullptr);
    grpc_server_shutdown_and_notify(g_server, shutdown_cq, tag(1000));
    GPR_ASSERT(grpc_completion_queue_pluck(shutdown_cq, tag(1000),
                                           grpc_timeout_seconds_to_deadline(5),
                                           nullptr)
                   .type == GRPC_OP_COMPLETE);
    grpc_server_destroy(g_server);
    grpc_completion_queue_shutdown(shutdown_cq);
    grpc_completion_queue_destroy(shutdown_cq);
    grpc_completion_queue_shutdown(g_cq);
    drain_cq(g_cq);
    grpc_completion_queue_destroy(g_cq);
    grpc_shutdown();
  }
  {
    grpc::testing::TestEnvironment env(argc, argv);
    grpc_init();
    // The entries in the global registry must still persist through
    // a full shutdown and restart of the library.
    grpc_channel_credentials* global_creds =
        create_test_ssl_plus_token_channel_creds("global-auth-header");
    auto global_key = grpc_core::UniquePtr<char>(gpr_strdup("global"));
    GPR_ASSERT(grpc_control_plane_credentials_register(global_key.get(),
                                                       global_creds) == false);
    grpc_channel_credentials_release(global_creds);
    // Sanity check that unmapped authorities can still register in
    // the global registry.
    grpc_channel_credentials* global_creds_2 =
        create_test_ssl_plus_token_channel_creds("global-auth-header");
    GPR_ASSERT(grpc_control_plane_credentials_register("global_2",
                                                       global_creds_2) == true);
    GPR_ASSERT(grpc_control_plane_credentials_register(
                   "global_2", global_creds_2) == false);
    grpc_channel_credentials_release(global_creds_2);
    grpc_shutdown();
  }
  return 0;
}
