/*
 *
 * Copyright 2018 gRPC authors.
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

#include <stdint.h>
#include <string.h>

#include <string>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

namespace grpc {
namespace testing {
namespace {

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now(void) {
  return n_seconds_from_now(5);
}

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

grpc_tls_certificate_provider* server_provider_create() {
  grpc_slice cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
  std::string identity_cert =
      std::string(grpc_core::StringViewFromSlice(cert_slice));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
  std::string private_key =
      std::string(grpc_core::StringViewFromSlice(key_slice));
  grpc_tls_identity_pairs* server_pairs = grpc_tls_identity_pairs_create();
  grpc_tls_identity_pairs_add_pair(server_pairs, private_key.c_str(),
                                   identity_cert.c_str());
  grpc_tls_certificate_provider* server_provider =
      grpc_tls_certificate_provider_static_data_create(nullptr, server_pairs);

  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  return server_provider;
}

grpc_tls_certificate_provider* client_provider_create() {
  grpc_slice root_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(CA_CERT_PATH, 1, &root_slice)));
  std::string root_cert =
      std::string(grpc_core::StringViewFromSlice(root_slice));
  grpc_tls_certificate_provider* client_provider =
      grpc_tls_certificate_provider_static_data_create(root_cert.c_str(),
                                                       nullptr);

  grpc_slice_unref(root_slice);
  return client_provider;
}

grpc_server* server_create(grpc_completion_queue* cq, const char* server_addr,
                           grpc_tls_version tls_version,
                           grpc_tls_certificate_provider* provider) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  grpc_tls_credentials_options_set_min_tls_version(options, tls_version);
  grpc_tls_credentials_options_set_max_tls_version(options, tls_version);
  // Set credential provider.
  grpc_tls_credentials_options_set_certificate_provider(options, provider);
  grpc_tls_credentials_options_watch_root_certs(options);
  grpc_tls_credentials_options_watch_identity_key_cert_pairs(options);
  // Set client certificate request type.
  grpc_tls_credentials_options_set_cert_request_type(
      options, GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
  // Set credential verifier.
  grpc_server_credentials* creds = grpc_tls_server_credentials_create(options);

  grpc_server* server = grpc_server_create(nullptr, nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  GPR_ASSERT(grpc_server_add_http2_port(server, server_addr, creds));
  grpc_server_credentials_release(creds);
  grpc_server_start(server);
  return server;
}

grpc_channel* client_create(const char* server_addr,
                            grpc_tls_version tls_version,
                            grpc_tls_certificate_provider* provider) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  grpc_tls_credentials_options_set_verify_server_cert(
      options, 1 /* = verify server certs */);
  grpc_tls_credentials_options_set_min_tls_version(options, tls_version);
  grpc_tls_credentials_options_set_max_tls_version(options, tls_version);
  grpc_tls_credentials_options_set_certificate_provider(options, provider);
  grpc_tls_credentials_options_watch_root_certs(options);

  // Create TLS channel credentials.
  grpc_channel_credentials* creds = grpc_tls_credentials_create(options);

  grpc_arg args[] = {
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
          const_cast<char*>("foo.test.google.fr")),
  };

  grpc_channel_args* client_args =
      grpc_channel_args_copy_and_add(nullptr, args, GPR_ARRAY_SIZE(args));

  grpc_channel* client = grpc_channel_create(server_addr, creds, client_args);
  GPR_ASSERT(client != nullptr);
  grpc_channel_credentials_release(creds);

  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(client_args);
  }
  return client;
}

static void shutdown_server(grpc_completion_queue* cq, grpc_server* server) {
  if (!server) return;
  grpc_server_shutdown_and_notify(server, cq, tag(1000));

  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), nullptr);
  } while (ev.type != GRPC_OP_COMPLETE || ev.tag != tag(1000));
  grpc_server_destroy(server);
}

static void make_request(grpc_completion_queue* cq, grpc_channel* client) {
  grpc_call* c;
  grpc_core::CqVerifier cqv(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(client, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
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

  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);

  grpc_call_unref(c);
}

TEST(H2TlsWrongVersionTest, ServerHasHigherTlsVersionThanClientCanSupport) {
  int port = grpc_pick_unused_port_or_die();

  std::string server_addr = grpc_core::JoinHostPort("localhost", port);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_tls_certificate_provider* server_provider = server_provider_create();
  grpc_tls_certificate_provider* client_provider = client_provider_create();
  grpc_server* server = server_create(
      cq, server_addr.c_str(), grpc_tls_version::TLS1_3, server_provider);
  grpc_channel* client = client_create(
      server_addr.c_str(), grpc_tls_version::TLS1_2, client_provider);

  make_request(cq, client);

  grpc_tls_certificate_provider_release(server_provider);
  grpc_tls_certificate_provider_release(client_provider);

  shutdown_server(cq, server);
  grpc_channel_destroy(client);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

TEST(H2TlsWrongVersionTest, ClientHasHigherTlsVersionThanServerCanSupport) {
  int port = grpc_pick_unused_port_or_die();

  std::string server_addr = grpc_core::JoinHostPort("localhost", port);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_tls_certificate_provider* server_provider = server_provider_create();
  grpc_tls_certificate_provider* client_provider = client_provider_create();
  grpc_server* server = server_create(
      cq, server_addr.c_str(), grpc_tls_version::TLS1_2, server_provider);
  grpc_channel* client = client_create(
      server_addr.c_str(), grpc_tls_version::TLS1_3, client_provider);

  make_request(cq, client);

  grpc_tls_certificate_provider_release(server_provider);
  grpc_tls_certificate_provider_release(client_provider);

  shutdown_server(cq, server);
  grpc_channel_destroy(client);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
