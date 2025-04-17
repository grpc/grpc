//
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

#include <grpc/grpc_security.h>
#include <grpcpp/security/auth_context.h>

#include "gtest/gtest.h"
#include "src/core/transport/auth_context.h"
#include "src/cpp/common/secure_auth_context.h"
#include "test/cpp/util/string_ref_helper.h"

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
    ctx_ = grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
    grpc_auth_context_add_cstring_property(ctx_.get(), "name", "chapi");
    grpc_auth_context_add_cstring_property(ctx_.get(), "name", "chapo");
    grpc_auth_context_add_cstring_property(ctx_.get(), "foo", "bar");
    EXPECT_EQ(1, grpc_auth_context_set_peer_identity_property_name(ctx_.get(),
                                                                   "name"));
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx_;
};

TEST_F(AuthPropertyIteratorTest, DefaultCtor) {
  TestAuthPropertyIterator iter1;
  TestAuthPropertyIterator iter2;
  EXPECT_EQ(iter1, iter2);
}

TEST_F(AuthPropertyIteratorTest, GeneralTest) {
  grpc_auth_property_iterator c_iter =
      grpc_auth_context_property_iterator(ctx_.get());
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
