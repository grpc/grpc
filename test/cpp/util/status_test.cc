// Copyright 2021 gRPC authors.
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

#include <gtest/gtest.h>

#include <grpcpp/support/status.h>

#include "test/core/util/test_config.h"

namespace grpc {
namespace {

TEST(StatusTest, Basics) {
  EXPECT_EQ(Status::OK.error_code(), StatusCode::OK);
  EXPECT_EQ(Status::OK.error_message(), "");
  EXPECT_EQ(Status::OK.error_details(), "");
  EXPECT_TRUE(Status::OK.ok());

  EXPECT_EQ(Status().error_code(), StatusCode::OK);
  EXPECT_EQ(Status().error_message(), "");
  EXPECT_EQ(Status().error_details(), "");
  EXPECT_TRUE(Status().ok());

  EXPECT_EQ(Status::CANCELLED.error_code(), StatusCode::CANCELLED);
  EXPECT_EQ(Status::CANCELLED.error_message(), "");
  EXPECT_EQ(Status::CANCELLED.error_details(), "");
  EXPECT_FALSE(Status::CANCELLED.ok());

  for (StatusCode status :
       {StatusCode::CANCELLED, StatusCode::UNKNOWN,
        StatusCode::INVALID_ARGUMENT, StatusCode::DEADLINE_EXCEEDED,
        StatusCode::NOT_FOUND, StatusCode::ALREADY_EXISTS,
        StatusCode::PERMISSION_DENIED, StatusCode::RESOURCE_EXHAUSTED,
        StatusCode::FAILED_PRECONDITION, StatusCode::ABORTED,
        StatusCode::OUT_OF_RANGE, StatusCode::UNIMPLEMENTED,
        StatusCode::INTERNAL, StatusCode::UNAVAILABLE, StatusCode::DATA_LOSS}) {
    for (const char* message : {"", "Hello world"}) {
      EXPECT_EQ(Status(status, message).error_code(), status);
      EXPECT_EQ(Status(status, message).error_message(), message);
      EXPECT_EQ(Status(status, message).error_details(), "");
      for (const char* details : {"", "Slartibartfast", "Trick or treat!"}) {
        EXPECT_EQ(Status(status, message, details).error_code(), status);
        EXPECT_EQ(Status(status, message, details).error_message(), message);
        EXPECT_EQ(Status(status, message, details).error_details(), details);
      }
    }
  }
}

TEST(StatusTest, AbslConversion) {
  absl::Status status = grpc::Status::OK;
  EXPECT_EQ(status, absl::OkStatus());
  status = grpc::Status::CANCELLED;
  EXPECT_EQ(status, absl::CancelledError(""));
  status = grpc::Status(grpc::StatusCode::UNKNOWN,
                        "Nobody expects the Spanish inquisition!");
  EXPECT_EQ(status,
            absl::UnknownError("Nobody expects the Spanish inquisition!"));
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
