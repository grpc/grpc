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

#include <gtest/gtest.h>
#include "absl/strings/string_view.h"

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "test/core/util/eval_args_mock_endpoint.h"

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
  EXPECT_EQ(src_address, local_address_)
      << "Error: Failed to extract correct Local address from EvaluateArgs.";
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsLocalPort) {
  int src_port = evaluate_args_->GetLocalPort();
  EXPECT_EQ(src_port, local_port_)
      << "Error: Failed to extract correct Local port from EvaluateArgs.";
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsPeerAddress) {
  absl::string_view dest_address = evaluate_args_->GetPeerAddress();
  EXPECT_EQ(dest_address, peer_address_)
      << "Error: Failed to extract correct Peer address from "
         "EvaluateArgs. ";
}

TEST_F(EvaluateArgsTest, TestEvaluateArgsPeerPort) {
  int dest_port = evaluate_args_->GetPeerPort();
  EXPECT_EQ(dest_port, peer_port_)
      << "Error: Failed to extract correct Peer port from EvaluateArgs.";
}

TEST(EvaluateArgsMetadataTest, HandlesNullMetadata) {
  EvaluateArgs evalArgs(nullptr, nullptr, nullptr);
  absl::string_view path = evalArgs.GetPath();
  absl::string_view method = evalArgs.GetMethod();
  absl::string_view host = evalArgs.GetHost();
  std::multimap<absl::string_view, absl::string_view> headers = evalArgs.GetHeaders();
  EXPECT_EQ(path, nullptr) << "Failed to return nullptr with null metadata_batch.";
  EXPECT_EQ(method, nullptr) << "Failed to return nullptr with null metadata_batch.";
  EXPECT_EQ(host, nullptr) << "Failed to return nullptr with null metadata_batch.";
  EXPECT_EQ(headers.size(), 0) << "Failed to return nullptr with null metadata_batch.";
}

TEST(EvaluateArgsMetadataTest, HandlesEmptyMetadata) {
  grpc_metadata_batch metadata_;
  grpc_metadata_batch_init(&metadata_);
  EvaluateArgs evalArgs(&metadata_, nullptr, nullptr);
  absl::string_view path = evalArgs.GetPath();
  absl::string_view method = evalArgs.GetMethod();
  absl::string_view host = evalArgs.GetHost();
  std::multimap<absl::string_view, absl::string_view> headers = evalArgs.GetHeaders();
  EXPECT_EQ(path, nullptr) << "Failed to return nullptr with null metadata_batch.";
  EXPECT_EQ(method, nullptr) << "Failed to return nullptr with null metadata_batch.";
  EXPECT_EQ(host, nullptr) << "Failed to return nullptr with null metadata_batch.";
  EXPECT_EQ(headers.size(), 0) << "Failed to return nullptr with null metadata_batch.";
}

TEST(EvaluateArgsMetadataTest, GetPathSuccess) {
  grpc_init();
  grpc_metadata_batch metadata_;
  absl::string_view get_val;
  grpc_metadata_batch_init(&metadata_);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string("/foo/bar"));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_val);
  grpc_linked_mdelem storage;
  // grpc_linked_mdelem* storage = new grpc_linked_mdelem;
  storage.md = fake_val_md;
  GRPC_LOG_IF_ERROR("failed to add to metadata batch", grpc_metadata_batch_link_head(&metadata_, &storage));
  EvaluateArgs evalArgs(&metadata_, nullptr, nullptr);
  // EvaluateArgs* evalArgs = new EvaluateArgs(&metadata_, nullptr, nullptr);
  get_val = evalArgs.GetPath();
  grpc_metadata_batch_destroy(&metadata_);
  EXPECT_EQ(get_val, "/foo/bar") << "Failed to properly set or retrieve path.";
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetHostSuccess) {
  // grpc_init();
  grpc_metadata_batch metadata_;
  grpc_metadata_batch_init(&metadata_);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string("foo"));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_HOST, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  GRPC_LOG_IF_ERROR("failed to add to metadata batch", grpc_metadata_batch_link_head(&metadata_, &storage));
  EvaluateArgs evalArgs(&metadata_, nullptr, nullptr);
  absl::string_view get_val = evalArgs.GetHost();
  EXPECT_EQ(get_val, "foo") << "Failed to properly set or retrieve host.";
  grpc_metadata_batch_destroy(&metadata_);
  // grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetMethodSuccess) {
  grpc_metadata_batch metadata_;
  absl::string_view get_val;
  grpc_metadata_batch_init(&metadata_);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string("GET"));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_METHOD, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  GRPC_LOG_IF_ERROR("failed to add to metadata batch", grpc_metadata_batch_link_head(&metadata_, &storage));
  EvaluateArgs evalArgs(&metadata_, nullptr, nullptr);
  get_val = evalArgs.GetMethod();
  EXPECT_EQ(get_val, "GET") << "Failed to properly set or retrieve method.";
  grpc_metadata_batch_destroy(&metadata_);
}

// TEST(EvaluateArgsMetadataTest, GetHeadersSuccess) {
//   grpc_init();
//   grpc_metadata_batch metadata_;
//   grpc_metadata_batch_init(&metadata_);
//   std::multimap<std::string, std::string> real_headers;
//   real_headers.emplace(GRPC_MDSTR_PATH, "/foo/bar");
//   // real_headers.emplace(GRPC_MDSTR_HOST, "foo");
//   // real_headers.emplace(GRPC_MDSTR_METHOD, "GET");
  
//   // grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string("/foo/bar"));
//   // grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_val);
//   // grpc_linked_mdelem* storage = new grpc_linked_mdelem;
//   // storage->md = fake_val_md;
//   // GRPC_LOG_IF_ERROR("failed to add to metadata batch", grpc_metadata_batch_link_head(&metadata_, storage));

// }



TEST(EvaluateArgsAuthContextTest, HandlesNullAuthContext) {
  EvaluateArgs evalArgs(nullptr, nullptr, nullptr);
  absl::string_view spiffe_id = evalArgs.GetSpiffeId();
  absl::string_view name = evalArgs.GetCertServerName();
  EXPECT_EQ(spiffe_id, nullptr) << "Failed to return nullptr with null auth_context.";
  EXPECT_EQ(name, nullptr) << "Failed to return nullptr with null auth_context.";
}

TEST(EvaluateArgsAuthContextTest, HandlesEmptyAuthCtx) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context auth_context_(ctx);
  ASSERT_NE(&auth_context_, nullptr) << "auth_context didint initialize?";

  EvaluateArgs evalArgs(nullptr, &auth_context_, nullptr);
  absl::string_view spiffe_id = evalArgs.GetSpiffeId();
  absl::string_view name = evalArgs.GetCertServerName();
  EXPECT_EQ(spiffe_id, nullptr) << "Failed to return nullptr with empty auth_context.";
  EXPECT_EQ(name, nullptr) << "Failed to return nullptr with empty auth_context.";
}




TEST(EvaluateArgsAuthContextTest, GetSpiffeIdSuccessOneProperty) {
  grpc_init();
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context auth_context_(ctx);
  ASSERT_NE(&auth_context_, nullptr) << "auth_context didint initialize?";
  
  auth_context_.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "test1");

  EvaluateArgs evalArgs(nullptr, &auth_context_, nullptr);
  absl::string_view id = evalArgs.GetSpiffeId();
  EXPECT_EQ(id, "test1") << "Failed to properly retrieve spiffe id";
  grpc_shutdown();
}

TEST(EvaluateArgsAuthContextTest, GetSpiffeIdFailDuplicateProperty) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context auth_context_(ctx);
  ASSERT_NE(&auth_context_, nullptr) << "auth_context didint initialize?";
  
  auth_context_.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "test1");  
  auth_context_.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "test2");

  EvaluateArgs evalArgs(nullptr, &auth_context_, nullptr);
  absl::string_view id = evalArgs.GetSpiffeId();
  EXPECT_EQ(id, nullptr) << "Failed to account for multiple properties";
}

TEST(EvaluateArgsAuthContextTest, GetCertServerNameSuccessOneProperty) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context auth_context_(ctx);
  ASSERT_NE(&auth_context_, nullptr) << "auth_context didint initialize?";
  
  auth_context_.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "test1");

  EvaluateArgs evalArgs(nullptr, &auth_context_, nullptr);
  absl::string_view name = evalArgs.GetCertServerName();
  EXPECT_EQ(name, "test1") << "Failed to properly retrieve cert server name";
}

TEST(EvaluateArgsAuthContextTest, GetCertServerNameFailDuplicateProperty) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context auth_context_(ctx);
  ASSERT_NE(&auth_context_, nullptr) << "auth_context didint initialize?";
  
  auth_context_.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "test1");  
  auth_context_.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "test2");
  EvaluateArgs evalArgs(nullptr, &auth_context_, nullptr);
  absl::string_view name = evalArgs.GetCertServerName();
  EXPECT_EQ(name, nullptr) << "Failed to account for multiple properties";
}



}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
