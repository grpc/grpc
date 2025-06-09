//
// Copyright 2025 gRPC authors.
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

#include "src/core/credentials/call/dual/dual_call_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/transport/alts/alts_security_connector.h"
#include "src/core/credentials/transport/composite/composite_channel_credentials.h"
#include "src/core/credentials/transport/google_default/google_default_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_call_creds.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

class DualCredentialsTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { grpc_init(); }

  static void TearDownTestSuite() { grpc_shutdown_blocking(); }

  void SetUp() override {
    auto* creds = reinterpret_cast<grpc_composite_channel_credentials*>(
        grpc_google_default_credentials_create(
            grpc_md_only_test_credentials_create(
                GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                GRPC_TLS_TRANSPORT_SECURITY_TYPE),
            grpc_md_only_test_credentials_create(
                GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                GRPC_ALTS_TRANSPORT_SECURITY_TYPE)));
    channel_creds_ = RefCountedPtr<grpc_composite_channel_credentials>(creds);
    pollent_ =
        grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  }

  void TearDown() override {
    grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
  }

  void RunRequestMetadataTest(
      grpc_call_credentials::GetRequestMetadataArgs get_request_metadata_args) {
    activity_ = MakeActivity(
        [this, &get_request_metadata_args] {
          return Map(channel_creds_->mutable_call_creds()->GetRequestMetadata(
                         ClientMetadataHandle(&expected_md_,
                                              Arena::PooledDeleter(nullptr)),
                         &get_request_metadata_args),
                     [](absl::StatusOr<ClientMetadataHandle> metadata) {
                       return metadata.status();
                     });
        },
        ExecCtxWakeupScheduler(),
        [](absl::Status status) mutable { ASSERT_TRUE(status.ok()); },
        arena_.get(), &pollent_);
  }

  RefCountedPtr<grpc_auth_context> CreateAuthContextWithSecurityType(
      std::string security_type) {
    auto auth_context = grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
    grpc_auth_context_add_property(
        auth_context.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
        security_type.c_str(), security_type.length());
    grpc_auth_context_set_peer_identity_property_name(
        auth_context.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
    return auth_context;
  }

  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  grpc_metadata_batch expected_md_;
  grpc_polling_entity pollent_;
  ActivityPtr activity_;
  RefCountedPtr<grpc_composite_channel_credentials> channel_creds_;
};

TEST_F(DualCredentialsTest, UseAltsCredentials) {
  ExecCtx exec_ctx;
  grpc_call_credentials::GetRequestMetadataArgs get_request_metadata_args = {
      nullptr,
      CreateAuthContextWithSecurityType(GRPC_ALTS_TRANSPORT_SECURITY_TYPE)};

  RunRequestMetadataTest(get_request_metadata_args);

  std::string buffer;
  EXPECT_EQ(expected_md_.GetStringValue(
                GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME, &buffer),
            GRPC_ALTS_TRANSPORT_SECURITY_TYPE);
}

TEST_F(DualCredentialsTest, UseTlsCredentials) {
  ExecCtx exec_ctx;
  grpc_call_credentials::GetRequestMetadataArgs get_request_metadata_args = {
      nullptr,
      CreateAuthContextWithSecurityType(GRPC_TLS_TRANSPORT_SECURITY_TYPE)};

  RunRequestMetadataTest(get_request_metadata_args);

  std::string buffer;
  EXPECT_EQ(expected_md_.GetStringValue(
                GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME, &buffer),
            GRPC_TLS_TRANSPORT_SECURITY_TYPE);
}

TEST_F(DualCredentialsTest, NoAuthContext) {
  ExecCtx exec_ctx;
  grpc_call_credentials::GetRequestMetadataArgs get_request_metadata_args = {
      nullptr, nullptr};

  RunRequestMetadataTest(get_request_metadata_args);

  std::string buffer;
  EXPECT_EQ(expected_md_.GetStringValue(
                GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME, &buffer),
            GRPC_TLS_TRANSPORT_SECURITY_TYPE);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
