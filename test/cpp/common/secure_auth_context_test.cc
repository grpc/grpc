/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/cpp/common/secure_auth_context.h"
#include <grpc/grpc_security.h>
#include <grpcpp/security/auth_context.h>
#include <gtest/gtest.h>
#include "test/cpp/util/string_ref_helper.h"

#include "src/core/lib/security/context/security_context.h"

using grpc::testing::ToString;

namespace grpc {
namespace {

class SecureAuthContextTest : public ::testing::Test {};

// Created with nullptr
TEST_F(SecureAuthContextTest, EmptyContext) {
  SecureAuthContext context(nullptr);
  EXPECT_TRUE(context.GetPeerIdentity().empty());
  EXPECT_TRUE(context.GetPeerIdentityPropertyName().empty());
  EXPECT_TRUE(context.FindPropertyValues("").empty());
  EXPECT_TRUE(context.FindPropertyValues("whatever").empty());
  EXPECT_TRUE(context.begin() == context.end());
}

TEST_F(SecureAuthContextTest, Properties) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  SecureAuthContext context(ctx.get());
  ctx.reset();
  context.AddProperty("name", "chapi");
  context.AddProperty("name", "chapo");
  context.AddProperty("foo", "bar");
  EXPECT_TRUE(context.SetPeerIdentityPropertyName("name"));

  std::vector<grpc::string_ref> peer_identity = context.GetPeerIdentity();
  EXPECT_EQ(2u, peer_identity.size());
  EXPECT_EQ("chapi", ToString(peer_identity[0]));
  EXPECT_EQ("chapo", ToString(peer_identity[1]));
  EXPECT_EQ("name", context.GetPeerIdentityPropertyName());
  std::vector<grpc::string_ref> bar = context.FindPropertyValues("foo");
  EXPECT_EQ(1u, bar.size());
  EXPECT_EQ("bar", ToString(bar[0]));
}

TEST_F(SecureAuthContextTest, Iterators) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  SecureAuthContext context(ctx.get());
  ctx.reset();
  context.AddProperty("name", "chapi");
  context.AddProperty("name", "chapo");
  context.AddProperty("foo", "bar");
  EXPECT_TRUE(context.SetPeerIdentityPropertyName("name"));

  AuthPropertyIterator iter = context.begin();
  EXPECT_TRUE(context.end() != iter);
  AuthProperty p0 = *iter;
  ++iter;
  AuthProperty p1 = *iter;
  iter++;
  AuthProperty p2 = *iter;
  EXPECT_EQ("name", ToString(p0.first));
  EXPECT_EQ("chapi", ToString(p0.second));
  EXPECT_EQ("name", ToString(p1.first));
  EXPECT_EQ("chapo", ToString(p1.second));
  EXPECT_EQ("foo", ToString(p2.first));
  EXPECT_EQ("bar", ToString(p2.second));
  ++iter;
  EXPECT_EQ(context.end(), iter);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
