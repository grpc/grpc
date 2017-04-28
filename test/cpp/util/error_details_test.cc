/*
 *
 * Copyright 2017, Google Inc.
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

#include <grpc++/support/error_details.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/status/status.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"

namespace grpc {
namespace {

TEST(ExtractTest, Success) {
  google::rpc::Status expected;
  expected.set_code(13);  // INTERNAL
  expected.set_message("I am an error message");
  testing::EchoRequest expected_details;
  expected_details.set_message(grpc::string(100, '\0'));
  expected.add_details()->PackFrom(expected_details);

  google::rpc::Status to;
  grpc::string error_details = expected.SerializeAsString();
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
  grpc::string error_details("I am not a status object");
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
  expected_details.set_message(grpc::string(100, '\0'));
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
  expected.set_code(20);  // Out of scope (DATA_LOSS is 15).
  expected.set_message("I am an error message");
  testing::EchoRequest expected_details;
  expected_details.set_message(grpc::string(100, '\0'));
  expected.add_details()->PackFrom(expected_details);

  Status to;
  Status s = SetErrorDetails(expected, &to);
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(StatusCode::UNKNOWN, to.error_code());
  EXPECT_EQ(expected.message(), to.error_message());
  EXPECT_EQ(expected.SerializeAsString(), to.error_details());
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
