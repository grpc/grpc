/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpcpp/security/server_credentials.h>
#include <memory>

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

namespace {

/** The 3 tests below ensure that the C-core is properly initialized when a
 *  server credentials instance is created by itself. **/
TEST(ServerCredentialsTest, AltsServerCredentials) {
  ::grpc_impl::experimental::AltsServerCredentialsOptions options;
  std::shared_ptr<::grpc_impl::ServerCredentials> server_credentials =
      grpc::experimental::AltsServerCredentials(options);
  EXPECT_NE(server_credentials.get(), nullptr);
}

TEST(ServerCredentialsTest, LocalServerCredentials) {
  std::shared_ptr<::grpc_impl::ServerCredentials> server_credentials =
      grpc::experimental::LocalServerCredentials(LOCAL_TCP);
  EXPECT_NE(server_credentials.get(), nullptr);
}

TEST(ServerCredentialsTest, SslServerCredentials) {
  grpc::SslServerCredentialsOptions options;
  std::shared_ptr<::grpc_impl::ServerCredentials> server_credentials =
      grpc::SslServerCredentials(options);
  EXPECT_NE(server_credentials.get(), nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
