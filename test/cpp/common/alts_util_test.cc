//
//
// Copyright 2019 gRPC authors.
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

#include <gtest/gtest.h>

#include "upb/upb.hpp"

#include <grpcpp/security/alts_context.h>
#include <grpcpp/security/alts_util.h>
#include <grpcpp/security/auth_context.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/cpp/common/secure_auth_context.h"
#include "src/proto/grpc/gcp/altscontext.upb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/string_ref_helper.h"

namespace grpc {
namespace {

TEST(AltsUtilTest, NullAuthContext) {
  std::unique_ptr<experimental::AltsContext> alts_context =
      experimental::GetAltsContextFromAuthContext(nullptr);
  EXPECT_EQ(alts_context, nullptr);
}

TEST(AltsUtilTest, EmptyAuthContext) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  const std::shared_ptr<AuthContext> auth_context(
      new SecureAuthContext(ctx.get()));
  std::unique_ptr<experimental::AltsContext> alts_context =
      experimental::GetAltsContextFromAuthContext(auth_context);
  EXPECT_EQ(alts_context, nullptr);
}

TEST(AltsUtilTest, AuthContextWithMoreThanOneAltsContext) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  const std::shared_ptr<AuthContext> auth_context(
      new SecureAuthContext(ctx.get()));
  ctx.reset();
  auth_context->AddProperty(TSI_ALTS_CONTEXT, "context1");
  auth_context->AddProperty(TSI_ALTS_CONTEXT, "context2");
  std::unique_ptr<experimental::AltsContext> alts_context =
      experimental::GetAltsContextFromAuthContext(auth_context);
  EXPECT_EQ(alts_context, nullptr);
}

TEST(AltsUtilTest, AuthContextWithBadAltsContext) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  const std::shared_ptr<AuthContext> auth_context(
      new SecureAuthContext(ctx.get()));
  ctx.reset();
  auth_context->AddProperty(TSI_ALTS_CONTEXT,
                            "bad context string serialization");
  std::unique_ptr<experimental::AltsContext> alts_context =
      experimental::GetAltsContextFromAuthContext(auth_context);
  EXPECT_EQ(alts_context, nullptr);
}

TEST(AltsUtilTest, AuthContextWithGoodAltsContextWithoutRpcVersions) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  const std::shared_ptr<AuthContext> auth_context(
      new SecureAuthContext(ctx.get()));
  ctx.reset();
  std::string expected_ap("application protocol");
  std::string expected_rp("record protocol");
  std::string expected_peer("peer");
  std::string expected_local("local");
  std::string expected_peer_atrributes_key("peer");
  std::string expected_peer_atrributes_value("attributes");
  grpc_security_level expected_sl = GRPC_INTEGRITY_ONLY;
  upb::Arena context_arena;
  grpc_gcp_AltsContext* context = grpc_gcp_AltsContext_new(context_arena.ptr());
  grpc_gcp_AltsContext_set_application_protocol(
      context,
      upb_StringView_FromDataAndSize(expected_ap.data(), expected_ap.length()));
  grpc_gcp_AltsContext_set_record_protocol(
      context,
      upb_StringView_FromDataAndSize(expected_rp.data(), expected_rp.length()));
  grpc_gcp_AltsContext_set_security_level(context, expected_sl);
  grpc_gcp_AltsContext_set_peer_service_account(
      context, upb_StringView_FromDataAndSize(expected_peer.data(),
                                              expected_peer.length()));
  grpc_gcp_AltsContext_set_local_service_account(
      context, upb_StringView_FromDataAndSize(expected_local.data(),
                                              expected_local.length()));
  grpc_gcp_AltsContext_peer_attributes_set(
      context,
      upb_StringView_FromDataAndSize(expected_peer_atrributes_key.data(),
                                     expected_peer_atrributes_key.length()),
      upb_StringView_FromDataAndSize(expected_peer_atrributes_value.data(),
                                     expected_peer_atrributes_value.length()),
      context_arena.ptr());
  size_t serialized_ctx_length;
  char* serialized_ctx = grpc_gcp_AltsContext_serialize(
      context, context_arena.ptr(), &serialized_ctx_length);
  EXPECT_NE(serialized_ctx, nullptr);
  auth_context->AddProperty(TSI_ALTS_CONTEXT,
                            string(serialized_ctx, serialized_ctx_length));
  std::unique_ptr<experimental::AltsContext> alts_context =
      experimental::GetAltsContextFromAuthContext(auth_context);
  EXPECT_NE(alts_context, nullptr);
  EXPECT_EQ(expected_ap, alts_context->application_protocol());
  EXPECT_EQ(expected_rp, alts_context->record_protocol());
  EXPECT_EQ(expected_peer, alts_context->peer_service_account());
  EXPECT_EQ(expected_local, alts_context->local_service_account());
  EXPECT_EQ(expected_sl, alts_context->security_level());
  // all rpc versions should be 0 if not set
  experimental::AltsContext::RpcProtocolVersions rpc_protocol_versions =
      alts_context->peer_rpc_versions();
  EXPECT_EQ(0, rpc_protocol_versions.max_rpc_version.major_version);
  EXPECT_EQ(0, rpc_protocol_versions.max_rpc_version.minor_version);
  EXPECT_EQ(0, rpc_protocol_versions.min_rpc_version.major_version);
  EXPECT_EQ(0, rpc_protocol_versions.min_rpc_version.minor_version);
  EXPECT_EQ(expected_peer_atrributes_value,
            alts_context->peer_attributes().at(expected_peer_atrributes_key));
}

TEST(AltsUtilTest, AuthContextWithGoodAltsContext) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  const std::shared_ptr<AuthContext> auth_context(
      new SecureAuthContext(ctx.get()));
  ctx.reset();
  upb::Arena context_arena;
  grpc_gcp_AltsContext* context = grpc_gcp_AltsContext_new(context_arena.ptr());
  upb::Arena versions_arena;
  grpc_gcp_RpcProtocolVersions* versions =
      grpc_gcp_RpcProtocolVersions_new(versions_arena.ptr());
  upb::Arena max_major_version_arena;
  grpc_gcp_RpcProtocolVersions_Version* version =
      grpc_gcp_RpcProtocolVersions_Version_new(max_major_version_arena.ptr());
  grpc_gcp_RpcProtocolVersions_Version_set_major(version, 10);
  grpc_gcp_RpcProtocolVersions_set_max_rpc_version(versions, version);
  grpc_gcp_AltsContext_set_peer_rpc_versions(context, versions);
  size_t serialized_ctx_length;
  char* serialized_ctx = grpc_gcp_AltsContext_serialize(
      context, context_arena.ptr(), &serialized_ctx_length);
  EXPECT_NE(serialized_ctx, nullptr);
  auth_context->AddProperty(TSI_ALTS_CONTEXT,
                            string(serialized_ctx, serialized_ctx_length));
  std::unique_ptr<experimental::AltsContext> alts_context =
      experimental::GetAltsContextFromAuthContext(auth_context);
  EXPECT_NE(alts_context, nullptr);
  EXPECT_EQ("", alts_context->application_protocol());
  EXPECT_EQ("", alts_context->record_protocol());
  EXPECT_EQ("", alts_context->peer_service_account());
  EXPECT_EQ("", alts_context->local_service_account());
  EXPECT_EQ(GRPC_SECURITY_NONE, alts_context->security_level());
  experimental::AltsContext::RpcProtocolVersions rpc_protocol_versions =
      alts_context->peer_rpc_versions();
  EXPECT_EQ(10, rpc_protocol_versions.max_rpc_version.major_version);
  EXPECT_EQ(0, rpc_protocol_versions.max_rpc_version.minor_version);
  EXPECT_EQ(0, rpc_protocol_versions.min_rpc_version.major_version);
  EXPECT_EQ(0, rpc_protocol_versions.min_rpc_version.minor_version);
}

TEST(AltsUtilTest, AltsClientAuthzCheck) {
  // AltsClientAuthzCheck function should return a permission denied error on
  // the bad_auth_context, whose internal ALTS context does not exist
  const std::shared_ptr<AuthContext> bad_auth_context(
      new SecureAuthContext(nullptr));
  std::vector<std::string> service_accounts{"client"};
  grpc::Status status =
      experimental::AltsClientAuthzCheck(bad_auth_context, service_accounts);
  EXPECT_EQ(grpc::StatusCode::PERMISSION_DENIED, status.error_code());
  // AltsClientAuthzCheck function should function normally when the peer name
  // in ALTS context is listed in service_accounts
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  const std::shared_ptr<AuthContext> auth_context(
      new SecureAuthContext(ctx.get()));
  ctx.reset();
  std::string peer("good_client");
  std::vector<std::string> good_service_accounts{"good_client",
                                                 "good_client_1"};
  std::vector<std::string> bad_service_accounts{"bad_client", "bad_client_1"};
  upb::Arena context_arena;
  grpc_gcp_AltsContext* context = grpc_gcp_AltsContext_new(context_arena.ptr());
  grpc_gcp_AltsContext_set_peer_service_account(
      context, upb_StringView_FromDataAndSize(peer.data(), peer.length()));
  size_t serialized_ctx_length;
  char* serialized_ctx = grpc_gcp_AltsContext_serialize(
      context, context_arena.ptr(), &serialized_ctx_length);
  EXPECT_NE(serialized_ctx, nullptr);
  auth_context->AddProperty(TSI_ALTS_CONTEXT,
                            string(serialized_ctx, serialized_ctx_length));
  grpc::Status good_status =
      experimental::AltsClientAuthzCheck(auth_context, good_service_accounts);
  EXPECT_TRUE(good_status.ok());
  grpc::Status bad_status =
      experimental::AltsClientAuthzCheck(auth_context, bad_service_accounts);
  EXPECT_EQ(grpc::StatusCode::PERMISSION_DENIED, bad_status.error_code());
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
