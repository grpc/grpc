//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <string.h>

#include <string>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

constexpr char kCaCertPath0[] = "src/core/tsi/test_creds/ca_p256.pem";
constexpr char kCaCertPath1[] = "src/core/tsi/test_creds/ca_p384.pem";
constexpr char kCaCertPath2[] = "src/core/tsi/test_creds/ca_p521.pem";
constexpr char kClientCertPath0[] = "src/core/tsi/test_creds/client_p256.pem";
constexpr char kClientKeyPath0[] = "src/core/tsi/test_creds/client_p256.key";
constexpr char kClientCertPath1[] = "src/core/tsi/test_creds/client_p384.pem";
constexpr char kClientKeyPath1[] = "src/core/tsi/test_creds/client_p384.key";
constexpr char kClientCertPath2[] = "src/core/tsi/test_creds/client_p521.pem";
constexpr char kClientKeyPath2[] = "src/core/tsi/test_creds/client_p521.key";
constexpr char kServerCertPath0[] = "src/core/tsi/test_creds/server1_p256.pem";
constexpr char kServerKeyPath0[] = "src/core/tsi/test_creds/server1_p256.key";
constexpr char kServerCertPath1[] = "src/core/tsi/test_creds/server1_p384.pem";
constexpr char kServerKeyPath1[] = "src/core/tsi/test_creds/server1_p384.key";
constexpr char kServerCertPath2[] = "src/core/tsi/test_creds/server1_p521.pem";
constexpr char kServerKeyPath2[] = "src/core/tsi/test_creds/server1_p521.key";

namespace grpc {
namespace testing {
namespace {

gpr_timespec five_seconds_time() { return grpc_timeout_seconds_to_deadline(5); }

grpc_server* server_create(grpc_completion_queue* cq, const char* server_addr,
                           const char* ca_cert_path,
                           const char* server_cert_path,
                           const char* server_key_path) {
  grpc_slice ca_slice, cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(ca_cert_path, 1, &ca_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(server_cert_path, 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(server_key_path, 1, &key_slice)));
  const char* ca_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
  const char* server_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
  const char* server_key =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {server_key, server_cert};
  auto* cert_config =
      grpc_ssl_server_certificate_config_create(ca_cert, &pem_cert_key_pair, 1);
  auto* options = grpc_ssl_server_credentials_create_options_using_config(
      GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, cert_config);
  grpc_server_credentials* server_creds =
      grpc_ssl_server_credentials_create_with_options(options);
  // This is a hack but we don't have a public API to force TLS version yet.
  reinterpret_cast<grpc_ssl_server_credentials*>(server_creds)
      ->set_max_tls_version(grpc_tls_version::TLS1_2);
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  GPR_ASSERT(grpc_server_add_http2_port(server, server_addr, server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(server);

  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  grpc_slice_unref(ca_slice);
  return server;
}

grpc_channel* client_create(const char* server_addr, const char* ca_cert_path,
                            const char* client_cert_path,
                            const char* client_key_path) {
  grpc_slice ca_slice, cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(ca_cert_path, 1, &ca_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(client_cert_path, 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(client_key_path, 1, &key_slice)));
  const char* ca_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
  const char* client_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
  const char* client_key =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
  grpc_ssl_pem_key_cert_pair signed_client_key_cert_pair = {client_key,
                                                            client_cert};
  grpc_channel_credentials* client_creds = grpc_ssl_credentials_create(
      ca_cert, &signed_client_key_cert_pair, nullptr, nullptr);

  grpc_arg args[] = {
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
          const_cast<char*>("waterzooi.test.google.be")),
  };

  grpc_channel_args* client_args =
      grpc_channel_args_copy_and_add(nullptr, args, GPR_ARRAY_SIZE(args));

  grpc_channel* client =
      grpc_channel_create(server_addr, client_creds, client_args);
  GPR_ASSERT(client != nullptr);
  grpc_channel_credentials_release(client_creds);

  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(client_args);
  }

  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  grpc_slice_unref(ca_slice);
  return client;
}

void do_round_trip(grpc_completion_queue* cq, grpc_server* server,
                   const char* server_addr, const char* ca_cert_path,
                   const char* client_cert_path, const char* client_key_path) {
  grpc_channel* client = client_create(server_addr, ca_cert_path,
                                       client_cert_path, client_key_path);

  grpc_core::CqVerifier cqv(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(60);
  grpc_call* c = grpc_channel_create_call(
      client, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/foo"), nullptr, deadline, nullptr);
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
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  grpc_call* s;
  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq,
                                   grpc_core::CqVerifier::tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(grpc_core::CqVerifier::tag(101), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(103), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(grpc_core::CqVerifier::tag(103), true);
  cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  cqv.Verify();

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  grpc_channel_destroy(client);
}

void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

TEST(H2SslCertsEllipicCurvesTest, RoundTripWithP256Curve) {
  int port = grpc_pick_unused_port_or_die();

  std::string server_addr = grpc_core::JoinHostPort("localhost", port);

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_server* server = server_create(cq, server_addr.c_str(), kCaCertPath0,
                                      kServerCertPath0, kServerKeyPath0);

  do_round_trip(cq, server, server_addr.c_str(), kCaCertPath0, kClientCertPath0,
                kClientKeyPath0);

  GPR_ASSERT(grpc_completion_queue_next(
                 cq, grpc_timeout_milliseconds_to_deadline(100), nullptr)
                 .type == GRPC_QUEUE_TIMEOUT);

  grpc_server_shutdown_and_notify(server, cq, grpc_core::CqVerifier::tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE ||
           ev.tag != grpc_core::CqVerifier::tag(1000));
  grpc_server_destroy(server);

  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

TEST(H2SslCertsEllipicCurvesTest, RoundTripWithP384Curve) {
  int port = grpc_pick_unused_port_or_die();

  std::string server_addr = grpc_core::JoinHostPort("localhost", port);

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_server* server = server_create(cq, server_addr.c_str(), kCaCertPath1,
                                      kServerCertPath1, kServerKeyPath1);

  do_round_trip(cq, server, server_addr.c_str(), kCaCertPath1, kClientCertPath1,
                kClientKeyPath1);

  GPR_ASSERT(grpc_completion_queue_next(
                 cq, grpc_timeout_milliseconds_to_deadline(100), nullptr)
                 .type == GRPC_QUEUE_TIMEOUT);

  grpc_server_shutdown_and_notify(server, cq, grpc_core::CqVerifier::tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE ||
           ev.tag != grpc_core::CqVerifier::tag(1000));
  grpc_server_destroy(server);

  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

TEST(H2SslCertsEllipicCurvesTest, RoundTripWithP521Curve) {
  int port = grpc_pick_unused_port_or_die();

  std::string server_addr = grpc_core::JoinHostPort("localhost", port);

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  grpc_server* server = server_create(cq, server_addr.c_str(), kCaCertPath2,
                                      kServerCertPath2, kServerKeyPath2);

  do_round_trip(cq, server, server_addr.c_str(), kCaCertPath2, kClientCertPath2,
                kClientKeyPath2);

  GPR_ASSERT(grpc_completion_queue_next(
                 cq, grpc_timeout_milliseconds_to_deadline(100), nullptr)
                 .type == GRPC_QUEUE_TIMEOUT);

  grpc_server_shutdown_and_notify(server, cq, grpc_core::CqVerifier::tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE ||
           ev.tag != grpc_core::CqVerifier::tag(1000));
  grpc_server_destroy(server);

  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::ConfigVars::Overrides overrides;
  grpc_core::ConfigVars::SetOverrides(overrides);

  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();

  return ret;
}
