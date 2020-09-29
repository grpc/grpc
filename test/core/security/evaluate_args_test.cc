// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/string_view.h"

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "test/core/util/eval_args_mock_endpoint.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

class EvaluateArgsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    local_address_ = "255.255.255.255";
    peer_address_ = "128.128.128.128";
    local_port_ = 413;
    peer_port_ = 314;
    endpoint_ = CreateEvalArgsMockEndpoint(local_address_.c_str(), local_port_,
                                           peer_address_.c_str(), peer_port_);
    evaluate_args_ =
        absl::make_unique<EvaluateArgs>(nullptr, nullptr, endpoint_);
  }
  void TearDown() override { grpc_endpoint_destroy(endpoint_); }
  grpc_endpoint* endpoint_;
  std::unique_ptr<EvaluateArgs> evaluate_args_;
  std::string local_address_;
  std::string peer_address_;
  int local_port_;
  int peer_port_;
};

TEST_F(EvaluateArgsTest, TestEvaluateArgsLocalAddress) {
  absl::string_view src_address = evaluate_args_->GetLocalAddress();
  EXPECT_EQ(src_address, local_address_);
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsLocalPort) {
  int src_port = evaluate_args_->GetLocalPort();
  EXPECT_EQ(src_port, local_port_);
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsPeerAddress) {
  absl::string_view dest_address = evaluate_args_->GetPeerAddress();
  EXPECT_EQ(dest_address, peer_address_);
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsPeerPort) {
  int dest_port = evaluate_args_->GetPeerPort();
  EXPECT_EQ(dest_port, peer_port_);
}

TEST(EvaluateArgsMetadataTest, HandlesNullMetadata) {
  EvaluateArgs eval_args(nullptr, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetPath(), nullptr);
  EXPECT_EQ(eval_args.GetMethod(), nullptr);
  EXPECT_EQ(eval_args.GetHost(), nullptr);
  EXPECT_THAT(eval_args.GetHeaders(), ::testing::ElementsAre());
}

TEST(EvaluateArgsMetadataTest, HandlesEmptyMetadata) {
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetPath(), nullptr);
  EXPECT_EQ(eval_args.GetMethod(), nullptr);
  EXPECT_EQ(eval_args.GetHost(), nullptr);
  EXPECT_THAT(eval_args.GetHeaders(), ::testing::ElementsAre());
  grpc_metadata_batch_destroy(&metadata);
}

TEST(EvaluateArgsMetadataTest, GetPathSuccess) {
  grpc_init();
  const char* kPath = "/some/path";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string(kPath));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetPath(), kPath);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetHostSuccess) {
  grpc_init();
  const char* kHost = "host";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string(kHost));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_HOST, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetHost(), kHost);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetMethodSuccess) {
  grpc_init();
  const char* kMethod = "GET";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val =
      grpc_slice_intern(grpc_slice_from_static_string(kMethod));
  grpc_mdelem fake_val_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_METHOD, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetMethod(), kMethod);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetHeadersSuccess) {
  grpc_init();
  const char* kPath = "/some/path";
  const char* kHost = "host";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_path =
      grpc_slice_intern(grpc_slice_from_static_string(kPath));
  grpc_mdelem fake_path_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_path);
  grpc_linked_mdelem storage;
  storage.md = fake_path_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage, GRPC_BATCH_PATH),
            GRPC_ERROR_NONE);
  grpc_slice fake_host =
      grpc_slice_intern(grpc_slice_from_static_string(kHost));
  grpc_mdelem fake_host_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_HOST, fake_host);
  grpc_linked_mdelem storage2;
  storage2.md = fake_host_md;
  ASSERT_EQ(
      grpc_metadata_batch_link_tail(&metadata, &storage2, GRPC_BATCH_HOST),
      GRPC_ERROR_NONE);
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_THAT(
      eval_args.GetHeaders(),
      ::testing::UnorderedElementsAre(
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_HOST), kHost),
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_PATH), kPath)));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsAuthContextTest, HandlesNullAuthContext) {
  EvaluateArgs eval_args(nullptr, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), nullptr);
  EXPECT_EQ(eval_args.GetCertServerName(), nullptr);
}

TEST(EvaluateArgsAuthContextTest, HandlesEmptyAuthCtx) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), nullptr);
  EXPECT_EQ(eval_args.GetCertServerName(), nullptr);
}

TEST(EvaluateArgsAuthContextTest, GetSpiffeIdSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kId = "spiffeid";
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, kId);
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), kId);
}

TEST(EvaluateArgsAuthContextTest, GetSpiffeIdFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id1");
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id2");
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), nullptr);
}

TEST(EvaluateArgsAuthContextTest, GetCertServerNameSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kServer = "server";
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, kServer);
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetCertServerName(), kServer);
}

TEST(EvaluateArgsAuthContextTest, GetCertServerNameFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server1");
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server2");
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetCertServerName(), nullptr);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
