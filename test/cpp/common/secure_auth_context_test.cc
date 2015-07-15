/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/auth_context.h>
#include <gtest/gtest.h>
#include "src/cpp/common/secure_auth_context.h"
#include "src/core/security/security_context.h"

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
}

TEST_F(SecureAuthContextTest, Properties) {
  grpc_auth_context* ctx = grpc_auth_context_create(NULL, 3);
  ctx->properties[0] = grpc_auth_property_init_from_cstring("name", "chapi");
  ctx->properties[1] = grpc_auth_property_init_from_cstring("name", "chapo");
  ctx->properties[2] = grpc_auth_property_init_from_cstring("foo", "bar");
  ctx->peer_identity_property_name = ctx->properties[0].name;

  SecureAuthContext context(ctx);
  std::vector<grpc::string> peer_identity = context.GetPeerIdentity();
  EXPECT_EQ(2u, peer_identity.size());
  EXPECT_EQ("chapi", peer_identity[0]);
  EXPECT_EQ("chapo", peer_identity[1]);
  EXPECT_EQ("name", context.GetPeerIdentityPropertyName());
  std::vector<grpc::string> bar = context.FindPropertyValues("foo");
  EXPECT_EQ(1u, bar.size());
  EXPECT_EQ("bar", bar[0]);
}

}  // namespace
}  // namespace grpc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
