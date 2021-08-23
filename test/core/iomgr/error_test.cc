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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <string.h>

#include "test/core/util/test_config.h"

static void test_set_get_int() {
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test");
  GPR_ASSERT(error);
  intptr_t i = 0;
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_FILE_LINE, &i));
  GPR_ASSERT(i);  // line set will never be 0
  GPR_ASSERT(!grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  GPR_ASSERT(!grpc_error_get_int(error, GRPC_ERROR_INT_SIZE, &i));

  intptr_t errnumber = 314;
  error = grpc_error_set_int(error, GRPC_ERROR_INT_ERRNO, errnumber);
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  GPR_ASSERT(i == errnumber);

  intptr_t http = 2;
  error = grpc_error_set_int(error, GRPC_ERROR_INT_HTTP2_ERROR, http);
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &i));
  GPR_ASSERT(i == http);

  GRPC_ERROR_UNREF(error);
}

static void test_set_get_str() {
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test");

  grpc_slice str;
  GPR_ASSERT(!grpc_error_get_str(error, GRPC_ERROR_STR_SYSCALL, &str));
  GPR_ASSERT(!grpc_error_get_str(error, GRPC_ERROR_STR_TSI_ERROR, &str));

  GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_FILE, &str));
  GPR_ASSERT(strstr((char*)GRPC_SLICE_START_PTR(str),
                    "error_test.c"));  // __FILE__ expands differently on
                                       // Windows. All should at least
                                       // contain error_test.c

  GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &str));
  GPR_ASSERT(!strncmp((char*)GRPC_SLICE_START_PTR(str), "Test",
                      GRPC_SLICE_LENGTH(str)));

  error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                             grpc_slice_from_static_string("longer message"));
  GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, &str));
  GPR_ASSERT(!strncmp((char*)GRPC_SLICE_START_PTR(str), "longer message",
                      GRPC_SLICE_LENGTH(str)));

  GRPC_ERROR_UNREF(error);
}

static void test_copy_and_unref() {
  // error1 has one ref
  grpc_error_handle error1 = grpc_error_set_str(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test"), GRPC_ERROR_STR_GRPC_MESSAGE,
      grpc_slice_from_static_string("message"));
  grpc_slice str;
  GPR_ASSERT(grpc_error_get_str(error1, GRPC_ERROR_STR_GRPC_MESSAGE, &str));
  GPR_ASSERT(!strncmp((char*)GRPC_SLICE_START_PTR(str), "message",
                      GRPC_SLICE_LENGTH(str)));

  // error 1 has two refs
  GRPC_ERROR_REF(error1);
  // this gives error3 a ref to the new error, and decrements error1 to one ref
  grpc_error_handle error3 = grpc_error_set_str(
      error1, GRPC_ERROR_STR_SYSCALL, grpc_slice_from_static_string("syscall"));
  GPR_ASSERT(error3 != error1);  // should not be the same because of extra ref
  GPR_ASSERT(grpc_error_get_str(error3, GRPC_ERROR_STR_GRPC_MESSAGE, &str));
  GPR_ASSERT(!strncmp((char*)GRPC_SLICE_START_PTR(str), "message",
                      GRPC_SLICE_LENGTH(str)));

  // error 1 should not have a syscall but 3 should
  GPR_ASSERT(!grpc_error_get_str(error1, GRPC_ERROR_STR_SYSCALL, &str));
  GPR_ASSERT(grpc_error_get_str(error3, GRPC_ERROR_STR_SYSCALL, &str));
  GPR_ASSERT(!strncmp((char*)GRPC_SLICE_START_PTR(str), "syscall",
                      GRPC_SLICE_LENGTH(str)));

  GRPC_ERROR_UNREF(error1);
  GRPC_ERROR_UNREF(error3);
}

static void test_create_referencing() {
  grpc_error_handle child = grpc_error_set_str(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child"),
      GRPC_ERROR_STR_GRPC_MESSAGE, grpc_slice_from_static_string("message"));
  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Parent", &child, 1);
  GPR_ASSERT(parent);

  GRPC_ERROR_UNREF(child);
  GRPC_ERROR_UNREF(parent);
}

static void test_create_referencing_many() {
  grpc_error_handle children[3];
  children[0] = grpc_error_set_str(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child1"),
      GRPC_ERROR_STR_GRPC_MESSAGE, grpc_slice_from_static_string("message"));
  children[1] =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child2"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 5);
  children[2] = grpc_error_set_str(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child3"),
      GRPC_ERROR_STR_GRPC_MESSAGE, grpc_slice_from_static_string("message 3"));

  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Parent", children, 3);
  GPR_ASSERT(parent);

  for (size_t i = 0; i < 3; ++i) {
    GRPC_ERROR_UNREF(children[i]);
  }
  GRPC_ERROR_UNREF(parent);
}

static void print_error_string() {
  grpc_error_handle error =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNIMPLEMENTED);
  error = grpc_error_set_int(error, GRPC_ERROR_INT_SIZE, 666);
  error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                             grpc_slice_from_static_string("message"));
  // gpr_log(GPR_DEBUG, "%s", grpc_error_std_string(error).c_str());
  GRPC_ERROR_UNREF(error);
}

static void print_error_string_reference() {
  grpc_error_handle children[2];
  children[0] = grpc_error_set_str(
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("1"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNIMPLEMENTED),
      GRPC_ERROR_STR_GRPC_MESSAGE,
      grpc_slice_from_static_string("message for child 1"));
  children[1] = grpc_error_set_str(
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("2sd"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL),
      GRPC_ERROR_STR_GRPC_MESSAGE,
      grpc_slice_from_static_string("message for child 2"));

  grpc_error_handle parent =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Parent", children, 2);

  for (size_t i = 0; i < 2; ++i) {
    GRPC_ERROR_UNREF(children[i]);
  }
  GRPC_ERROR_UNREF(parent);
}

static void test_os_error() {
  int fake_errno = 5;
  const char* syscall = "syscall name";
  grpc_error_handle error = GRPC_OS_ERROR(fake_errno, syscall);

  intptr_t i = 0;
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  GPR_ASSERT(i == fake_errno);

  grpc_slice str;
  GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_SYSCALL, &str));
  GPR_ASSERT(!strncmp((char*)GRPC_SLICE_START_PTR(str), syscall,
                      GRPC_SLICE_LENGTH(str)));
  GRPC_ERROR_UNREF(error);
}

static void test_overflow() {
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Overflow");

  for (size_t i = 0; i < 150; ++i) {
    error = grpc_error_add_child(error,
                                 GRPC_ERROR_CREATE_FROM_STATIC_STRING("Child"));
  }

  error = grpc_error_set_int(error, GRPC_ERROR_INT_HTTP2_ERROR, 5);
  error =
      grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                         grpc_slice_from_static_string("message for child 2"));
  error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, 5);

  intptr_t i;
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &i));
  GPR_ASSERT(i == 5);
  GPR_ASSERT(!grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &i));

  error = grpc_error_set_int(error, GRPC_ERROR_INT_HTTP2_ERROR, 10);
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &i));
  GPR_ASSERT(i == 10);

  GRPC_ERROR_UNREF(error);
  ;
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_set_get_int();
  test_set_get_str();
  test_copy_and_unref();
  print_error_string();
  print_error_string_reference();
  test_os_error();
  test_create_referencing();
  test_create_referencing_many();
  test_overflow();
  grpc_shutdown();

  return 0;
}
