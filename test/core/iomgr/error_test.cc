/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/iomgr/error.h"

#include <string.h>

#include <gmock/gmock.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

TEST(ErrorTest, SetGetInt) {
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test");
  EXPECT_NE(error, GRPC_ERROR_NONE);
  intptr_t i = 0;
#ifndef NDEBUG
  // GRPC_ERROR_INT_FILE_LINE is for debug only
  EXPECT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_FILE_LINE, &i));
  EXPECT_TRUE(i);  // line set will never be 0
#endif
  EXPECT_TRUE(!grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  EXPECT_TRUE(!grpc_error_get_int(error, GRPC_ERROR_INT_SIZE, &i));

  intptr_t errnumber = 314;
  error = grpc_error_set_int(error, GRPC_ERROR_INT_ERRNO, errnumber);
  EXPECT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  EXPECT_EQ(i, errnumber);

  intptr_t http = 2;
  error = grpc_error_set_int(error, GRPC_ERROR_INT_HTTP2_ERROR, http);
  EXPECT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &i));
  EXPECT_EQ(i, http);

  GRPC_ERROR_UNREF(error);
}

TEST(ErrorTest, SetGetStr) {
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test");

  std::string str;
  EXPECT_TRUE(!grpc_error_get_str(error, GRPC_ERROR_STR_SYSCALL, &str));
  EXPECT_TRUE(!grpc_error_get_str(error, GRPC_ERROR_STR_TSI_ERROR, &str));
#ifndef NDEBUG
  // GRPC_ERROR_STR_FILE is for debug only
  EXPECT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_FILE, &str));
  EXPECT_THAT(str, testing::HasSubstr("error_test.c"));
  // __FILE__ expands differently on
  // Windows. All should at least
  // contain error_test.c
#endif
  EXPECT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &str));
  EXPECT_EQ(str, "Test");

  error =
      grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, "longer message");
  EXPECT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, &str));
  EXPECT_EQ(str, "longer message");

  GRPC_ERROR_UNREF(error);
}

TEST(ErrorTest, CopyAndUnRef) {
  // error1 has one ref
  grpc_error_handle error1 =
      grpc_error_set_str(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test"),
                         GRPC_ERROR_STR_GRPC_MESSAGE, "message");
  std::string str;
  EXPECT_TRUE(grpc_error_get_str(error1, GRPC_ERROR_STR_GRPC_MESSAGE, &str));
  EXPECT_EQ(str, "message");

  // error 1 has two refs
  GRPC_ERROR_REF(error1);
  // this gives error3 a ref to the new error, and decrements error1 to one ref
  grpc_error_handle error3 =
      grpc_error_set_str(error1, GRPC_ERROR_STR_SYSCALL, "syscall");
  EXPECT_NE(error3, error1);  // should not be the same because of extra ref
  EXPECT_TRUE(grpc_error_get_str(error3, GRPC_ERROR_STR_GRPC_MESSAGE, &str));
  EXPECT_EQ(str, "message");

  // error 1 should not have a syscall but 3 should
  EXPECT_TRUE(!grpc_error_get_str(error1, GRPC_ERROR_STR_SYSCALL, &str));
  EXPECT_TRUE(grpc_error_get_str(error3, GRPC_ERROR_STR_SYSCALL, &str));
  EXPECT_EQ(str, "syscall");

  GRPC_ERROR_UNREF(error1);
  GRPC_ERROR_UNREF(error3);
}

TEST(ErrorTest, CreateReferencing) {
  grpc_error_handle child =
      grpc_error_set_str(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child"),
                         GRPC_ERROR_STR_GRPC_MESSAGE, "message");
  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Parent", &child, 1);
  EXPECT_NE(parent, GRPC_ERROR_NONE);

  GRPC_ERROR_UNREF(child);
  GRPC_ERROR_UNREF(parent);
}

TEST(ErrorTest, CreateReferencingMany) {
  grpc_error_handle children[3];
  children[0] =
      grpc_error_set_str(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child1"),
                         GRPC_ERROR_STR_GRPC_MESSAGE, "message");
  children[1] =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child2"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 5);
  children[2] =
      grpc_error_set_str(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child3"),
                         GRPC_ERROR_STR_GRPC_MESSAGE, "message 3");

  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Parent", children, 3);
  EXPECT_NE(parent, GRPC_ERROR_NONE);

  for (size_t i = 0; i < 3; ++i) {
    GRPC_ERROR_UNREF(children[i]);
  }
  GRPC_ERROR_UNREF(parent);
}

TEST(ErrorTest, PrintErrorString) {
  grpc_error_handle error =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNIMPLEMENTED);
  error = grpc_error_set_int(error, GRPC_ERROR_INT_SIZE, 666);
  error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, "message");
  // gpr_log(GPR_DEBUG, "%s", grpc_error_std_string(error).c_str());
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorTest, PrintErrorStringReference) {
  grpc_error_handle children[2];
  children[0] = grpc_error_set_str(
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("1"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNIMPLEMENTED),
      GRPC_ERROR_STR_GRPC_MESSAGE, "message for child 1");
  children[1] = grpc_error_set_str(
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("2sd"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL),
      GRPC_ERROR_STR_GRPC_MESSAGE, "message for child 2");

  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Parent", children, 2);

  for (size_t i = 0; i < 2; ++i) {
    GRPC_ERROR_UNREF(children[i]);
  }
  GRPC_ERROR_UNREF(parent);
}

TEST(ErrorTest, TestOsError) {
  int fake_errno = 5;
  const char* syscall = "syscall name";
  grpc_error_handle error = GRPC_OS_ERROR(fake_errno, syscall);

  intptr_t i = 0;
  EXPECT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  EXPECT_EQ(i, fake_errno);

  std::string str;
  EXPECT_TRUE(grpc_error_get_str(error, GRPC_ERROR_STR_SYSCALL, &str));
  EXPECT_EQ(str, syscall);
  GRPC_ERROR_UNREF(error);
}

TEST(ErrorTest, Overflow) {
  // absl::Status doesn't have a limit so there is no overflow
#ifndef GRPC_ERROR_IS_ABSEIL_STATUS
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Overflow");

  for (size_t i = 0; i < 150; ++i) {
    error = grpc_error_add_child(error,
                                 GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child"));
  }

  error = grpc_error_set_int(error, GRPC_ERROR_INT_HTTP2_ERROR, 5);
  error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                             "message for child 2");
  error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, 5);

  intptr_t i;
  EXPECT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &i));
  EXPECT_EQ(i, 5);
  EXPECT_TRUE(!grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &i));

  error = grpc_error_set_int(error, GRPC_ERROR_INT_HTTP2_ERROR, 10);
  EXPECT_TRUE(grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &i));
  EXPECT_EQ(i, 10);

  GRPC_ERROR_UNREF(error);
#endif
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
