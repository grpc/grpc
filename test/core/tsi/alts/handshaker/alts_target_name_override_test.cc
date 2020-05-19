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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <set>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <grpcpp/impl/codegen/service_type.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/alts/alts_security_connector.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include "test/core/tsi/alts/fake_handshaker/fake_handshaker_server.h"
#include "test/core/util/memory_counters.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/end2end/cq_verifier.h"

namespace {

void* tag(int i) { return (void*)static_cast<intptr_t>(i); }

class FakeHandshakeServer {
 public:
  FakeHandshakeServer(bool check_num_concurrent_rpcs,
                      const std::string& expected_target_name) {
    int port = grpc_pick_unused_port_or_die();
    address_ = grpc_core::JoinHostPort("localhost", port);
    service_ = grpc::gcp::CreateFakeHandshakerService(
        0 /* expected max concurrent rpcs unset */, expected_target_name);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address_.c_str(),
                             grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Fake handshaker server listening on %s",
            address_.c_str());
  }

  ~FakeHandshakeServer() {
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  }

  const char* address() { return address_.c_str(); }

 private:
  std::string address_;
  std::unique_ptr<grpc::Service> service_;
  std::unique_ptr<grpc::Server> server_;
};

// Perform a simple RPC and capture the value of the authority header
// metadata sent to the server, as a string.
std::string PerformCallAndGetAuthorityHeader(grpc_channel* channel,
                                             grpc_server* server,
                                             grpc_completion_queue* cq) {
  grpc_call* c;
  grpc_call* s;
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
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  int was_cancelled;
  // Start a call
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
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
  // Request a call on the server
  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
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
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
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
  // Extract the authority header
  std::string authority_header(
      grpc_core::StringViewFromSlice(call_details.host));
  // cleanup
  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
  return authority_header;
}

// Perform a few ALTS handshakes sequentially (using the fake, in-process ALTS
// handshake server).
TEST(AltsTargetNameOverrideTest,
     TestOverriddenTargetNameAffectsIsInHandshakeAndAuthorityHeader) {
  FakeHandshakeServer client_fake_handshake_server(
      false /* check num concurrent RPCs */, "alts.test.name.override");
  FakeHandshakeServer server_fake_handshake_server(
      false /* check num concurrent RPCs */,
      "" /* expected target name unset */);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  // create the server
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  std::string server_address =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_alts_credentials_options* alts_server_options =
      grpc_alts_credentials_server_options_create();
  grpc_server_credentials* server_creds =
      grpc_alts_server_credentials_create_customized(
          alts_server_options, server_fake_handshake_server.address(),
          true /* enable_untrusted_alts */);
  GPR_ASSERT(grpc_server_add_secure_http2_port(server, server_address.c_str(),
                                               server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_alts_credentials_options_destroy(alts_server_options);
  grpc_server_start(server);
  // create the channel
  grpc_alts_credentials_options* alts_client_options =
      grpc_alts_credentials_client_options_create();
  grpc_channel_credentials* channel_creds =
      grpc_alts_credentials_create_customized(
          alts_client_options, client_fake_handshake_server.address(),
          true /* enable_untrusted_alts */);
  grpc_arg alts_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_ALTS_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("alts.test.name.override")}};
  grpc_channel_args* channel_args =
      grpc_channel_args_copy_and_add(nullptr, &alts_name_override, 1);
  grpc_channel* channel = grpc_secure_channel_create(
      channel_creds, server_address.c_str(), channel_args, nullptr);
  grpc_channel_args_destroy(channel_args);
  grpc_channel_credentials_release(channel_creds);
  grpc_alts_credentials_options_destroy(alts_client_options);
  // perform an RPC
  std::string authority_header =
      PerformCallAndGetAuthorityHeader(channel, server, cq);
  // shutdown and destroy the client and server
  grpc_channel_destroy(channel);
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN)
    ;
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  // Verify that the alts target override was sent in the authority header.
  // Note that we verify that "alts.test.name.override" was sent in the
  // target_name field of the client's ALTS handshake indirectly, by passing
  // this expected target name to the FakeHandshakeServer, and seeing that
  // this test doesn't abort when the FakeHandshakeServer makes the comparison.
  EXPECT_EQ(authority_header, "alts.test.name.override");
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
