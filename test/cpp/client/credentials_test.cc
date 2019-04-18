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

#include <grpcpp/security/credentials.h>

#include <memory>

#include <grpc/grpc.h>
#include <gtest/gtest.h>

namespace grpc {
namespace testing {

class CredentialsTest : public ::testing::Test {
 protected:
};

TEST_F(CredentialsTest, InvalidGoogleRefreshToken) {
  std::shared_ptr<CallCredentials> bad1 = GoogleRefreshTokenCredentials("");
  EXPECT_EQ(static_cast<CallCredentials*>(nullptr), bad1.get());
}

TEST_F(CredentialsTest, DefaultCredentials) {
  auto creds = GoogleDefaultCredentials();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
