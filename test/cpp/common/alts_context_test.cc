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

#include <grpcpp/alts_context.h>
#include <grpcpp/security/auth_context.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/cpp/common/secure_auth_context.h"
#include "src/proto/grpc/gcp/altscontext.pb.h"
#include "test/cpp/util/string_ref_helper.h"

#include <gtest/gtest.h>

using grpc::testing::ToString;

namespace grpc {
namespace {

TEST(AltsContextTest, EmptyAuthContext) {
  SecureAuthContext context(nullptr);
  std::unique_ptr<gcp::AltsContext> alts_context =
      GetAltsContextFromAuthContext(context);
  EXPECT_EQ(alts_context.get(), nullptr);
}

TEST(AltsContextTest, AuthContextWithMoreThanOneAltsContext) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  SecureAuthContext context(ctx.get());
  ctx.reset();
  context.AddProperty(TSI_ALTS_CONTEXT, "context1");
  context.AddProperty(TSI_ALTS_CONTEXT, "context2");
  std::unique_ptr<gcp::AltsContext> alts_context =
      GetAltsContextFromAuthContext(context);
  EXPECT_EQ(alts_context.get(), nullptr);
}

TEST(AltsContextTest, AuthContextWithBadAltsContext) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  SecureAuthContext context(ctx.get());
  ctx.reset();
  context.AddProperty(TSI_ALTS_CONTEXT, "bad context string serialization");
  std::unique_ptr<gcp::AltsContext> alts_context =
      GetAltsContextFromAuthContext(context);
  EXPECT_EQ(alts_context.get(), nullptr);
}

TEST(AltsContextTest, AuthContextWithGoodAltsContext) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  SecureAuthContext context(ctx.get());
  ctx.reset();
  std::string expected_application_protocol("application protocol");
  std::string expected_record_protocol("record protocol");
  std::string expected_peer_id("peer");
  std::string expected_local_id("local");
  gcp::AltsContext expect_alts_ctx;
  expect_alts_ctx.set_application_protocol(expected_application_protocol);
  expect_alts_ctx.set_record_protocol(expected_record_protocol);
  expect_alts_ctx.set_peer_service_account(expected_peer_id);
  expect_alts_ctx.set_local_service_account(expected_local_id);
  std::string serialized_alts_ctx;
  bool success = expect_alts_ctx.SerializeToString(&serialized_alts_ctx);
  EXPECT_TRUE(success);
  context.AddProperty(TSI_ALTS_CONTEXT, serialized_alts_ctx);
  std::unique_ptr<gcp::AltsContext> alts_ctx =
      GetAltsContextFromAuthContext(context);
  EXPECT_EQ(expected_application_protocol, alts_ctx->application_protocol());
  EXPECT_EQ(expected_record_protocol, alts_ctx->record_protocol());
  EXPECT_EQ(expected_peer_id, alts_ctx->peer_service_account());
  EXPECT_EQ(expected_local_id, alts_ctx->local_service_account());
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
