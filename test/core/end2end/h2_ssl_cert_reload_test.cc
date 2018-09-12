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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/mutex_lock.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {
namespace {

void* tag(intptr_t t) { return (void*)t; }

gpr_timespec five_seconds_time() { return grpc_timeout_seconds_to_deadline(5); }

grpc_server* server_create(grpc_completion_queue* cq, const char* server_addr,
                           const char* root_cert, const char* server_key,
                           const char* server_cert) {
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {server_key, server_cert};
  grpc_server_credentials* server_creds = grpc_ssl_server_credentials_create_ex(
      root_cert, &pem_cert_key_pair, 1,
      GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, nullptr);

  grpc_server* server = grpc_server_create(nullptr, nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  GPR_ASSERT(
      grpc_server_add_secure_http2_port(server, server_addr, server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(server);

  return server;
}

struct next_client_ssl_config_result {
  gpr_mu lock;
  grpc_ssl_certificate_config_reload_status status;
  grpc_ssl_channel_certificate_config* config;
};

grpc_ssl_certificate_config_reload_status next_client_ssl_config(
    void* user_data, grpc_ssl_channel_certificate_config** config) {
  auto result = static_cast<next_client_ssl_config_result*>(user_data);
  grpc_core::MutexLock lock(&result->lock);
  *config = result->config;
  result->config = nullptr;
  auto status = result->status;
  if (status == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW) {
    result->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  }
  return status;
}

grpc_channel* client_create(char* server_addr,
                            next_client_ssl_config_result* result) {
  grpc_channel_credentials* client_creds =
      grpc_ssl_credentials_create_using_config_fetcher(
          next_client_ssl_config, result, nullptr, nullptr);

  grpc_arg args[] = {
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
          const_cast<char*>("waterzooi.test.google.be")),
  };

  grpc_channel_args* client_args =
      grpc_channel_args_copy_and_add(nullptr, args, GPR_ARRAY_SIZE(args));

  grpc_channel* client = grpc_secure_channel_create(client_creds, server_addr,
                                                    client_args, nullptr);
  GPR_ASSERT(client != nullptr);
  grpc_channel_credentials_release(client_creds);

  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(client_args);
  }

  return client;
}

void wait_for_connectivity(grpc_completion_queue* cq, grpc_channel* client,
                           grpc_connectivity_state target_state) {
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  cq_verifier* cqv = cq_verifier_create(cq);
  auto last_connectivity = grpc_channel_check_connectivity_state(client, 1);
  while (last_connectivity != target_state) {
    ASSERT_LT(gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline), 0);
    grpc_channel_watch_connectivity_state(client, last_connectivity, deadline,
                                          cq, tag(110));
    CQ_EXPECT_COMPLETION(cqv, tag(110), 1);
    cq_verify(cqv);
    last_connectivity = grpc_channel_check_connectivity_state(client, 1);
  }
  cq_verifier_destroy(cqv);
}

void do_round_trip(grpc_completion_queue* cq, grpc_channel* client,
                   const char* server_addr, const char* root_cert,
                   const char* server_key, const char* server_cert,
                   const char* expected_common_name) {
  cq_verifier* cqv = cq_verifier_create(cq);
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

  wait_for_connectivity(cq, client, GRPC_CHANNEL_TRANSIENT_FAILURE);
  grpc_server* server =
      server_create(cq, server_addr, root_cert, server_key, server_cert);
  wait_for_connectivity(cq, client, GRPC_CHANNEL_READY);

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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  grpc_call* s;
  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  grpc_auth_context* auth = grpc_call_auth_context(s);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth, GRPC_X509_CN_PROPERTY_NAME);
  const grpc_auth_property* property = grpc_auth_property_iterator_next(&it);
  GPR_ASSERT(property != nullptr);
  ASSERT_EQ(std::string(property->value, property->value_length),
            expected_common_name);
  grpc_auth_context_release(auth);

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
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  GPR_ASSERT(grpc_completion_queue_next(
                 cq, grpc_timeout_milliseconds_to_deadline(100), nullptr)
                 .type == GRPC_QUEUE_TIMEOUT);

  grpc_completion_queue* shutdown_cq =
      grpc_completion_queue_create_for_pluck(nullptr);
  grpc_server_shutdown_and_notify(server, shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         nullptr)
                 .type == GRPC_OP_COMPLETE);
  grpc_completion_queue_destroy(shutdown_cq);
  grpc_server_destroy(server);
}

void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

grpc_ssl_channel_certificate_config* ssl_new_config(const char* root_cert,
                                                    const char* key,
                                                    const char* cert) {
  grpc_ssl_pem_key_cert_pair key_cert_pair = {key, cert};
  return grpc_ssl_channel_certificate_config_create(root_cert, &key_cert_pair);
}

class FileContent {
 public:
  explicit FileContent(const char* path) {
    GPR_ASSERT(
        GRPC_LOG_IF_ERROR("load_file", grpc_load_file(path, 1, &slice_)));
  }
  ~FileContent() { grpc_slice_unref(slice_); }

  const char* Get() const {
    return reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(slice_));
  }

 private:
  grpc_slice slice_;
};

TEST(H2CertReloadTest, ReloadClientCert) {
  int port = grpc_pick_unused_port_or_die();

  FileContent root_cert("src/core/tsi/test_creds/ca.pem");
  FileContent client0_key("src/core/tsi/test_creds/client.key");
  FileContent client0_cert("src/core/tsi/test_creds/client.pem");
  FileContent client1_key("src/core/tsi/test_creds/client1.key");
  FileContent client1_cert("src/core/tsi/test_creds/client1.pem");
  FileContent server1_key("src/core/tsi/test_creds/server1.key");
  FileContent server1_cert("src/core/tsi/test_creds/server1.pem");

  char* server_addr;
  gpr_join_host_port(&server_addr, "localhost", port);

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  next_client_ssl_config_result result;
  gpr_mu_init(&result.lock);
  result.status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
  result.config =
      ssl_new_config(root_cert.Get(), client0_key.Get(), client0_cert.Get());

  grpc_channel* client = client_create(server_addr, &result);
  do_round_trip(cq, client, server_addr, root_cert.Get(), server1_key.Get(),
                server1_cert.Get(), "testclient");
  do_round_trip(cq, client, server_addr, root_cert.Get(), server1_key.Get(),
                server1_cert.Get(), "testclient");
  {
    grpc_core::MutexLock lock(&result.lock);
    result.status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL;
  }
  do_round_trip(cq, client, server_addr, root_cert.Get(), server1_key.Get(),
                server1_cert.Get(), "testclient");
  {
    grpc_core::MutexLock lock(&result.lock);
    result.status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
    result.config =
        ssl_new_config(root_cert.Get(), client1_key.Get(), client1_cert.Get());
  }
  do_round_trip(cq, client, server_addr, root_cert.Get(), server1_key.Get(),
                server1_cert.Get(), "testclient1");
  {
    grpc_core::MutexLock lock(&result.lock);
    result.status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
    result.config =
        ssl_new_config(root_cert.Get(), client1_key.Get(), client1_cert.Get());
  }
  do_round_trip(cq, client, server_addr, root_cert.Get(), server1_key.Get(),
                server1_cert.Get(), "testclient1");
  {
    grpc_core::MutexLock lock(&result.lock);
    result.status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
    result.config =
        ssl_new_config(root_cert.Get(), client0_key.Get(), client0_cert.Get());
  }
  do_round_trip(cq, client, server_addr, root_cert.Get(), server1_key.Get(),
                server1_cert.Get(), "testclient");

  grpc_channel_destroy(client);
  gpr_free(server_addr);
  grpc_ssl_session_cache_destroy(cache);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();

  return ret;
}
