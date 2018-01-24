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

#include <grpc/support/cmdline.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

#define LOG_TEST() gpr_log(GPR_INFO, "test at %s:%d", __FILE__, __LINE__)

static void test_simple_int(void) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("-foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 3);
  gpr_cmdline_destroy(cl);
}

static void test_eq_int(void) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("-foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 3);
  gpr_cmdline_destroy(cl);
}

static void test_2dash_int(void) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 3);
  gpr_cmdline_destroy(cl);
}

static void test_2dash_eq_int(void) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 3);
  gpr_cmdline_destroy(cl);
}

static void test_simple_string(void) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("-foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(0 == strcmp(x, "3"));
  gpr_cmdline_destroy(cl);
}

static void test_eq_string(void) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("-foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(0 == strcmp(x, "3"));
  gpr_cmdline_destroy(cl);
}

static void test_2dash_string(void) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(0 == strcmp(x, "3"));
  gpr_cmdline_destroy(cl);
}

static void test_2dash_eq_string(void) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(0 == strcmp(x, "3"));
  gpr_cmdline_destroy(cl);
}

static void test_flag_on(void) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 1);
  gpr_cmdline_destroy(cl);
}

static void test_flag_no(void) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--no-foo")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 0);
  gpr_cmdline_destroy(cl);
}

static void test_flag_val_1(void) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo=1")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 1);
  gpr_cmdline_destroy(cl);
}

static void test_flag_val_0(void) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo=0")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 0);
  gpr_cmdline_destroy(cl);
}

static void test_flag_val_true(void) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo=true")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 1);
  gpr_cmdline_destroy(cl);
}

static void test_flag_val_false(void) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--foo=false")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  GPR_ASSERT(x == 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 0);
  gpr_cmdline_destroy(cl);
}

static void test_many(void) {
  const char* str = nullptr;
  int x = 0;
  int flag = 2;
  gpr_cmdline* cl;

  char* args[] = {(char*)__FILE__, const_cast<char*>("--str"),
                  const_cast<char*>("hello"), const_cast<char*>("-x=4"),
                  const_cast<char*>("-no-flag")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(x == 4);
  GPR_ASSERT(0 == strcmp(str, "hello"));
  GPR_ASSERT(flag == 0);
  gpr_cmdline_destroy(cl);
}

static void extra_arg_cb(void* user_data, const char* arg) {
  int* count = static_cast<int*>(user_data);
  GPR_ASSERT(arg != nullptr);
  GPR_ASSERT(strlen(arg) == 1);
  GPR_ASSERT(arg[0] == 'a' + *count);
  ++*count;
}

static void test_extra(void) {
  gpr_cmdline* cl;
  int count = 0;
  char* args[] = {(char*)__FILE__, const_cast<char*>("a"),
                  const_cast<char*>("b"), const_cast<char*>("c")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           &count);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(count == 3);
  gpr_cmdline_destroy(cl);
}

static void test_extra_dashdash(void) {
  gpr_cmdline* cl;
  int count = 0;
  char* args[] = {(char*)__FILE__, const_cast<char*>("--"),
                  const_cast<char*>("a"), const_cast<char*>("b"),
                  const_cast<char*>("c")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           &count);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  GPR_ASSERT(count == 3);
  gpr_cmdline_destroy(cl);
}

static void test_usage(void) {
  gpr_cmdline* cl;
  char* usage;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  usage = gpr_cmdline_usage_string(cl, "test");
  GPR_ASSERT(0 == strcmp(usage,
                         "Usage: test [--str=string] [--x=int] "
                         "[--flag|--no-flag] [file...]\n"));
  gpr_free(usage);

  usage = gpr_cmdline_usage_string(cl, "/foo/test");
  GPR_ASSERT(0 == strcmp(usage,
                         "Usage: test [--str=string] [--x=int] "
                         "[--flag|--no-flag] [file...]\n"));
  gpr_free(usage);

  gpr_cmdline_destroy(cl);
}

static void test_help(void) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* help[] = {(char*)__FILE__, const_cast<char*>("-h")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  GPR_ASSERT(0 == gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(help), help));

  gpr_cmdline_destroy(cl);
}

static void test_badargs1(void) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_arg_name[] = {(char*)__FILE__, const_cast<char*>("--y")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  GPR_ASSERT(0 ==
             gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_arg_name), bad_arg_name));

  gpr_cmdline_destroy(cl);
}

static void test_badargs2(void) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_int_value[] = {(char*)__FILE__, const_cast<char*>("--x"),
                           const_cast<char*>("henry")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  GPR_ASSERT(
      0 == gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_int_value), bad_int_value));

  gpr_cmdline_destroy(cl);
}

static void test_badargs3(void) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_bool_value[] = {(char*)__FILE__, const_cast<char*>("--flag=henry")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  GPR_ASSERT(0 == gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_bool_value),
                                    bad_bool_value));

  gpr_cmdline_destroy(cl);
}

static void test_badargs4(void) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_bool_value[] = {(char*)__FILE__, const_cast<char*>("--no-str")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  GPR_ASSERT(0 == gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_bool_value),
                                    bad_bool_value));

  gpr_cmdline_destroy(cl);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test_simple_int();
  test_eq_int();
  test_2dash_int();
  test_2dash_eq_int();
  test_simple_string();
  test_eq_string();
  test_2dash_string();
  test_2dash_eq_string();
  test_flag_on();
  test_flag_no();
  test_flag_val_1();
  test_flag_val_0();
  test_flag_val_true();
  test_flag_val_false();
  test_many();
  test_extra();
  test_extra_dashdash();
  test_usage();
  test_help();
  test_badargs1();
  test_badargs2();
  test_badargs3();
  test_badargs4();
  return 0;
}
