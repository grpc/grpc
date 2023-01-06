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

#include <string.h>

#include <gmock/gmock.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "test/core/util/test_config.h"

TEST(ErrorTest, SetGetInt) {
  grpc_error_handle error = GRPC_ERROR_CREATE("Test");
  EXPECT_NE(error, absl::OkStatus());
  intptr_t i = 0;
#ifndef NDEBUG
  // grpc_core::StatusIntProperty::kFileLine is for debug only
  EXPECT_TRUE(
      grpc_error_get_int(error, grpc_core::StatusIntProperty::kFileLine, &i));
  EXPECT_TRUE(i);  // line set will never be 0
#endif
  EXPECT_TRUE(
      !grpc_error_get_int(error, grpc_core::StatusIntProperty::kErrorNo, &i));
  EXPECT_TRUE(
      !grpc_error_get_int(error, grpc_core::StatusIntProperty::kSize, &i));

  intptr_t errnumber = 314;
  error = grpc_error_set_int(error, grpc_core::StatusIntProperty::kErrorNo,
                             errnumber);
  EXPECT_TRUE(
      grpc_error_get_int(error, grpc_core::StatusIntProperty::kErrorNo, &i));
  EXPECT_EQ(i, errnumber);

  intptr_t http = 2;
  error = grpc_error_set_int(error, grpc_core::StatusIntProperty::kHttp2Error,
                             http);
  EXPECT_TRUE(
      grpc_error_get_int(error, grpc_core::StatusIntProperty::kHttp2Error, &i));
  EXPECT_EQ(i, http);
}

TEST(ErrorTest, SetGetStr) {
  grpc_error_handle error = GRPC_ERROR_CREATE("Test");

  std::string str;
  EXPECT_TRUE(
      !grpc_error_get_str(error, grpc_core::StatusStrProperty::kSyscall, &str));
  EXPECT_TRUE(!grpc_error_get_str(
      error, grpc_core::StatusStrProperty::kTsiError, &str));
#ifndef NDEBUG
  // grpc_core::StatusStrProperty::kFile   is for debug only
  EXPECT_TRUE(
      grpc_error_get_str(error, grpc_core::StatusStrProperty::kFile, &str));
  EXPECT_THAT(str, testing::HasSubstr("error_test.c"));
  // __FILE__ expands differently on
  // Windows. All should at least
  // contain error_test.c
#endif
  EXPECT_TRUE(grpc_error_get_str(
      error, grpc_core::StatusStrProperty::kDescription, &str));
  EXPECT_EQ(str, "Test");

  error = grpc_error_set_str(error, grpc_core::StatusStrProperty::kGrpcMessage,
                             "longer message");
  EXPECT_TRUE(grpc_error_get_str(
      error, grpc_core::StatusStrProperty::kGrpcMessage, &str));
  EXPECT_EQ(str, "longer message");
}

TEST(ErrorTest, CopyAndUnRef) {
  // error1 has one ref
  grpc_error_handle error1 =
      grpc_error_set_str(GRPC_ERROR_CREATE("Test"),
                         grpc_core::StatusStrProperty::kGrpcMessage, "message");
  std::string str;
  EXPECT_TRUE(grpc_error_get_str(
      error1, grpc_core::StatusStrProperty::kGrpcMessage, &str));
  EXPECT_EQ(str, "message");

  // this gives error3 a ref to the new error, and decrements error1 to one ref
  grpc_error_handle error3 = grpc_error_set_str(
      error1, grpc_core::StatusStrProperty::kSyscall, "syscall");
  EXPECT_NE(error3, error1);  // should not be the same because of extra ref
  EXPECT_TRUE(grpc_error_get_str(
      error3, grpc_core::StatusStrProperty::kGrpcMessage, &str));
  EXPECT_EQ(str, "message");

  // error 1 should not have a syscall but 3 should
  EXPECT_TRUE(!grpc_error_get_str(
      error1, grpc_core::StatusStrProperty::kSyscall, &str));
  EXPECT_TRUE(
      grpc_error_get_str(error3, grpc_core::StatusStrProperty::kSyscall, &str));
  EXPECT_EQ(str, "syscall");
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
  error = grpc_error_set_int(error, grpc_core::StatusIntProperty::kSize, 666);
  error = grpc_error_set_str(error, grpc_core::StatusStrProperty::kGrpcMessage,
                             "message");
  // gpr_log(GPR_DEBUG, "%s", grpc_core::StatusToString(error).c_str());
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

  intptr_t i = 0;
  EXPECT_TRUE(
      grpc_error_get_int(error, grpc_core::StatusIntProperty::kErrorNo, &i));
  EXPECT_EQ(i, fake_errno);

  std::string str;
  EXPECT_TRUE(
      grpc_error_get_str(error, grpc_core::StatusStrProperty::kSyscall, &str));
  EXPECT_EQ(str, syscall);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
