//
//
// Copyright 2017 gRPC authors.
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

#include <grpcpp/support/error_details.h>

#include "google/rpc/status.pb.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace {

TEST(ExtractTest, Success) {
  google::rpc::Status expected;
  expected.set_code(13);  // INTERNAL
  expected.set_message("I am an error message");
  testing::EchoRequest expected_details;
  expected_details.set_message(std::string(100, '\0'));
  expected.add_details()->PackFrom(expected_details);

  google::rpc::Status to;
  std::string error_details = expected.SerializeAsString();
  Status from(static_cast<StatusCode>(expected.code()), expected.message(),
              error_details);
  EXPECT_TRUE(ExtractErrorDetails(from, &to).ok());
  EXPECT_EQ(expected.code(), to.code());
  EXPECT_EQ(expected.message(), to.message());
  EXPECT_EQ(1, to.details_size());
  testing::EchoRequest details;
  to.details(0).UnpackTo(&details);
  EXPECT_EQ(expected_details.message(), details.message());
}

TEST(ExtractTest, NullInput) {
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION,
            ExtractErrorDetails(Status(), nullptr).error_code());
}

TEST(ExtractTest, Unparsable) {
  std::string error_details("I am not a status object");
  Status from(StatusCode::INTERNAL, "", error_details);
  google::rpc::Status to;
  EXPECT_EQ(StatusCode::INVALID_ARGUMENT,
            ExtractErrorDetails(from, &to).error_code());
}

TEST(SetTest, Success) {
  google::rpc::Status expected;
  expected.set_code(13);  // INTERNAL
  expected.set_message("I am an error message");
  testing::EchoRequest expected_details;
  expected_details.set_message(std::string(100, '\0'));
  expected.add_details()->PackFrom(expected_details);

  Status to;
  Status s = SetErrorDetails(expected, &to);
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(expected.code(), to.error_code());
  EXPECT_EQ(expected.message(), to.error_message());
  EXPECT_EQ(expected.SerializeAsString(), to.error_details());
}

TEST(SetTest, NullInput) {
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION,
            SetErrorDetails(google::rpc::Status(), nullptr).error_code());
}

TEST(SetTest, OutOfScopeErrorCode) {
  google::rpc::Status expected;
  expected.set_code(17);  // Out of scope (UNAUTHENTICATED is 16).
  expected.set_message("I am an error message");
  testing::EchoRequest expected_details;
  expected_details.set_message(std::string(100, '\0'));
  expected.add_details()->PackFrom(expected_details);

  Status to;
  Status s = SetErrorDetails(expected, &to);
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(StatusCode::UNKNOWN, to.error_code());
  EXPECT_EQ(expected.message(), to.error_message());
  EXPECT_EQ(expected.SerializeAsString(), to.error_details());
}

TEST(SetTest, ValidScopeErrorCode) {
  for (int c = StatusCode::CANCELLED; c <= StatusCode::UNAUTHENTICATED; c++) {
    google::rpc::Status expected;
    expected.set_code(c);
    expected.set_message("I am an error message");
    testing::EchoRequest expected_details;
    expected_details.set_message(std::string(100, '\0'));
    expected.add_details()->PackFrom(expected_details);

    Status to;
    Status s = SetErrorDetails(expected, &to);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(c, to.error_code());
    EXPECT_EQ(expected.message(), to.error_message());
    EXPECT_EQ(expected.SerializeAsString(), to.error_details());
  }
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
