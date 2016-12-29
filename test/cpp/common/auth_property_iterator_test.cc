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

#include <grpc++/security/auth_context.h>
#include <grpc/grpc_security.h>
#include <gtest/gtest.h>
#include "src/cpp/common/secure_auth_context.h"
#include "test/cpp/util/string_ref_helper.h"

extern "C" {
#include "src/core/lib/security/context/security_context.h"
}

using ::grpc::testing::ToString;

namespace grpc {
namespace {

class TestAuthPropertyIterator : public AuthPropertyIterator {
 public:
  TestAuthPropertyIterator() {}
  TestAuthPropertyIterator(const grpc_auth_property* property,
                           const grpc_auth_property_iterator* iter)
      : AuthPropertyIterator(property, iter) {}
};

class AuthPropertyIteratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ctx_ = grpc_auth_context_create(NULL);
    grpc_auth_context_add_cstring_property(ctx_, "name", "chapi");
    grpc_auth_context_add_cstring_property(ctx_, "name", "chapo");
    grpc_auth_context_add_cstring_property(ctx_, "foo", "bar");
    EXPECT_EQ(1,
              grpc_auth_context_set_peer_identity_property_name(ctx_, "name"));
  }
  void TearDown() override { grpc_auth_context_release(ctx_); }
  grpc_auth_context* ctx_;
};

TEST_F(AuthPropertyIteratorTest, DefaultCtor) {
  TestAuthPropertyIterator iter1;
  TestAuthPropertyIterator iter2;
  EXPECT_EQ(iter1, iter2);
}

TEST_F(AuthPropertyIteratorTest, GeneralTest) {
  grpc_auth_property_iterator c_iter =
      grpc_auth_context_property_iterator(ctx_);
  const grpc_auth_property* property =
      grpc_auth_property_iterator_next(&c_iter);
  TestAuthPropertyIterator iter(property, &c_iter);
  TestAuthPropertyIterator empty_iter;
  EXPECT_FALSE(iter == empty_iter);
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
  EXPECT_EQ(empty_iter, iter);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
