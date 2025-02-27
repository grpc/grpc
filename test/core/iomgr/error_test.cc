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

#include "src/core/lib/iomgr/error.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <string.h>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "src/core/util/crash.h"
#include "src/core/util/strerror.h"
#include "test/core/test_util/test_config.h"

TEST(ErrorTest, SetGetInt) {
  grpc_error_handle error = GRPC_ERROR_CREATE("Test");
  EXPECT_NE(error, absl::OkStatus());
  intptr_t i = 0;
  EXPECT_TRUE(
      !grpc_error_get_int(error, grpc_core::StatusIntProperty::kStreamId, &i));
  EXPECT_TRUE(!grpc_error_get_int(
      error, grpc_core::StatusIntProperty::kHttp2Error, &i));

  intptr_t http = 2;
  error = grpc_error_set_int(error, grpc_core::StatusIntProperty::kHttp2Error,
                             http);
  EXPECT_TRUE(
      grpc_error_get_int(error, grpc_core::StatusIntProperty::kHttp2Error, &i));
  EXPECT_EQ(i, http);
}

TEST(ErrorTest, SetGetStr) {
  grpc_error_handle error = GRPC_ERROR_CREATE("Test");

  error = grpc_error_set_str(error, grpc_core::StatusStrProperty::kGrpcMessage,
                             "longer message");
  std::string str;
  EXPECT_TRUE(grpc_error_get_str(
      error, grpc_core::StatusStrProperty::kGrpcMessage, &str));
  EXPECT_EQ(str, "longer message");
}

TEST(ErrorTest, CopyAndUnRef) {
  // error1 has one ref
  grpc_error_handle error1 = grpc_error_set_int(
      GRPC_ERROR_CREATE("Test"), grpc_core::StatusIntProperty::kStreamId, 1);
  intptr_t i;
  EXPECT_TRUE(
      grpc_error_get_int(error1, grpc_core::StatusIntProperty::kStreamId, &i));
  EXPECT_EQ(i, 1);

  // this gives error3 a ref to the new error, and decrements error1 to one ref
  grpc_error_handle error3 =
      grpc_error_set_int(error1, grpc_core::StatusIntProperty::kHttp2Error, 2);
  EXPECT_NE(error3, error1);  // should not be the same because of extra ref
  EXPECT_TRUE(grpc_error_get_int(
      error3, grpc_core::StatusIntProperty::kHttp2Error, &i));
  EXPECT_EQ(i, 2);

  // error 1 should not have kHttp2Error
  EXPECT_FALSE(grpc_error_get_int(
      error1, grpc_core::StatusIntProperty::kHttp2Error, &i));
}

TEST(ErrorTest, CreateReferencing) {
  grpc_error_handle child =
      grpc_error_set_str(GRPC_ERROR_CREATE("Child"),
                         grpc_core::StatusStrProperty::kGrpcMessage, "message");
  grpc_error_handle parent = GRPC_ERROR_CREATE_REFERENCING("Parent", &child, 1);
  EXPECT_NE(parent, absl::OkStatus());
}

TEST(ErrorTest, CreateReferencingMany) {
  grpc_error_handle children[3];
  children[0] =
      grpc_error_set_str(GRPC_ERROR_CREATE("Child1"),
                         grpc_core::StatusStrProperty::kGrpcMessage, "message");
  children[1] =
      grpc_error_set_int(GRPC_ERROR_CREATE("Child2"),
                         grpc_core::StatusIntProperty::kHttp2Error, 5);
  children[2] = grpc_error_set_str(GRPC_ERROR_CREATE("Child3"),
                                   grpc_core::StatusStrProperty::kGrpcMessage,
                                   "message 3");

  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING("Parent", children, 3);
  EXPECT_NE(parent, absl::OkStatus());

  for (size_t i = 0; i < 3; ++i) {
  }
}

TEST(ErrorTest, PrintErrorString) {
  grpc_error_handle error = grpc_error_set_int(
      GRPC_ERROR_CREATE("Error"), grpc_core::StatusIntProperty::kRpcStatus,
      GRPC_STATUS_UNIMPLEMENTED);
  error =
      grpc_error_set_int(error, grpc_core::StatusIntProperty::kHttp2Error, 666);
  error = grpc_error_set_str(error, grpc_core::StatusStrProperty::kGrpcMessage,
                             "message");
  //  VLOG(2) << grpc_core::StatusToString(error);
}

TEST(ErrorTest, PrintErrorStringReference) {
  grpc_error_handle children[2];
  children[0] = grpc_error_set_str(
      grpc_error_set_int(GRPC_ERROR_CREATE("1"),
                         grpc_core::StatusIntProperty::kRpcStatus,
                         GRPC_STATUS_UNIMPLEMENTED),
      grpc_core::StatusStrProperty::kGrpcMessage, "message for child 1");
  children[1] = grpc_error_set_str(
      grpc_error_set_int(GRPC_ERROR_CREATE("2sd"),
                         grpc_core::StatusIntProperty::kRpcStatus,
                         GRPC_STATUS_INTERNAL),
      grpc_core::StatusStrProperty::kGrpcMessage, "message for child 2");

  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING("Parent", children, 2);

  for (size_t i = 0; i < 2; ++i) {
  }
}

TEST(ErrorTest, TestOsError) {
  int fake_errno = 5;
  const char* syscall = "syscall name";
  grpc_error_handle error = GRPC_OS_ERROR(fake_errno, syscall);
  EXPECT_EQ(error.message(),
            absl::StrCat("syscall name: ", grpc_core::StrError(5), " (5)"));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
