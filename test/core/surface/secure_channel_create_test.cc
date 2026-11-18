//
// Copyright 2015 gRPC authors.
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

#include <grpc/grpc.h>

#include <memory>

#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

class SecureChannelCreateTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { grpc_init(); }
  static void TearDownTestSuite() { grpc_shutdown(); }

  // Sends a simple RPC (headers only, no messages), which should fail
  // with the specified status.
  static void ExpectRpcFailure(grpc_channel* channel, absl::Status expected) {
    // Create CQ and verifier.
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_core::CqVerifier cqv(cq);
    // Create call.
    grpc_slice host = grpc_slice_from_static_string("anywhere");
    grpc_call* call = grpc_channel_create_call(
        channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
        grpc_slice_from_static_string("/Foo"), &host,
        grpc_timeout_seconds_to_deadline(100), nullptr);
    ASSERT_NE(call, nullptr);
    // Construct batch.
    grpc_metadata_array initial_metadata_recv;
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_metadata_array trailing_metadata_recv;
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_status_code status;
    grpc_slice details;
    grpc_op ops[4];
    memset(ops, 0, sizeof(ops));
    grpc_op* op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    ++op;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    ++op;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    ++op;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    ++op;
    // Start batch.
    grpc_call_error error =
        grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops),
                              grpc_core::CqVerifier::tag(1), nullptr);
    ASSERT_EQ(GRPC_CALL_OK, error);
    // Expect completion.  The call should immediately fail.
    cqv.Expect(grpc_core::CqVerifier::tag(1), true);
    cqv.Verify();
    // Check status.
    absl::Status actual_status(static_cast<absl::StatusCode>(status),
                               grpc_core::StringViewFromSlice(details));
    EXPECT_EQ(actual_status, expected);
    grpc_call_unref(call);
    grpc_completion_queue_destroy(cq);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
    grpc_slice_unref(details);
  }
};

TEST_F(SecureChannelCreateTest, UnknownSchemeTargetReturnsLameChannel) {
  grpc_core::CoreConfiguration::RunWithSpecialConfiguration(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        // Avoid default prefix
        builder->resolver_registry()->Reset();
      },
      []() {
        grpc_channel_credentials* creds =
            grpc_fake_transport_security_credentials_create();
        grpc_channel* channel =
            grpc_channel_create("blah://blah", creds, nullptr);
        ExpectRpcFailure(
            channel,
            // TODO(roth): This should probably use INTERNAL instead.
            absl::UnknownError(
                "Failed to create channel to 'blah://blah':"
                "the target uri is not valid: dns:///blah://blah"));
        grpc_channel_destroy(channel);
        grpc_channel_credentials_release(creds);
      });
}

TEST_F(SecureChannelCreateTest,
       SecurityConnectorAlreadyInArgsReturnsLameChannel) {
  grpc_arg arg = grpc_security_connector_to_arg(nullptr);
  grpc_channel_args args;
  args.num_args = 1;
  args.args = &arg;
  grpc_channel* channel = grpc_channel_create(nullptr, nullptr, &args);
  ExpectRpcFailure(
      channel, absl::InternalError(
                   "Failed to create channel to '':channel target is NULL"));
  grpc_channel_destroy(channel);
}

TEST_F(SecureChannelCreateTest, NullChannelCredsReturnsLameChannel) {
  grpc_channel* channel = grpc_channel_create("example.com", nullptr, nullptr);
  ExpectRpcFailure(
      channel,
      absl::InternalError(
          "Failed to create channel to 'example.com':No credentials provided"));
  grpc_channel_destroy(channel);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
