//
//
// Copyright 2015 gRPC authors.
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

#include "test/core/test_util/cmdline.h"

#include <string.h>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/test_config.h"

#define LOG_TEST() LOG(INFO) << "test at " << __FILE__ << ":" << __LINE__

TEST(CmdlineTest, SimpleInt) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("-foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 3);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, EqInt) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("-foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 3);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, 2DashInt) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 3);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, 2DashEqInt) {
  int x = 1;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_int(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 1);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 3);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, SimpleString) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("-foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_STREQ(x, "3");
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, EqString) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("-foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_STREQ(x, "3");
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, 2DashString) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo"),
                  const_cast<char*>("3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_STREQ(x, "3");
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, 2DashEqString) {
  const char* x = nullptr;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo=3")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, nullptr);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_STREQ(x, "3");
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, FlagOn) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 1);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, FlagNo) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--no-foo")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 0);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, FlagVal1) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo=1")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 1);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, FlagVal0) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo=0")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 0);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, FlagValTrue) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--foo=true")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 1);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, FlagValFalse) {
  int x = 2;
  gpr_cmdline* cl;
  char* args[] = {const_cast<char*>(__FILE__),
                  const_cast<char*>("--foo=false")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_flag(cl, "foo", nullptr, &x);
  ASSERT_EQ(x, 2);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 0);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, Many) {
  const char* str = nullptr;
  int x = 0;
  int flag = 2;
  gpr_cmdline* cl;

  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--str"),
                  const_cast<char*>("hello"), const_cast<char*>("-x=4"),
                  const_cast<char*>("-no-flag")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(x, 4);
  ASSERT_STREQ(str, "hello");
  ASSERT_EQ(flag, 0);
  gpr_cmdline_destroy(cl);
}

static void extra_arg_cb(void* user_data, const char* arg) {
  int* count = static_cast<int*>(user_data);
  ASSERT_NE(arg, nullptr);
  ASSERT_EQ(strlen(arg), 1);
  ASSERT_EQ(arg[0], 'a' + *count);
  ++*count;
}

TEST(CmdlineTest, Extra) {
  gpr_cmdline* cl;
  int count = 0;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("a"),
                  const_cast<char*>("b"), const_cast<char*>("c")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           &count);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(count, 3);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, ExtraDashdash) {
  gpr_cmdline* cl;
  int count = 0;
  char* args[] = {const_cast<char*>(__FILE__), const_cast<char*>("--"),
                  const_cast<char*>("a"), const_cast<char*>("b"),
                  const_cast<char*>("c")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           &count);
  gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(args), args);
  ASSERT_EQ(count, 3);
  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, Usage) {
  gpr_cmdline* cl;

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

  std::string usage = gpr_cmdline_usage_string(cl, "test");
  ASSERT_EQ(usage,
            "Usage: test [--str=string] [--x=int] "
            "[--flag|--no-flag] [file...]\n");

  usage = gpr_cmdline_usage_string(cl, "/foo/test");
  ASSERT_EQ(usage,
            "Usage: test [--str=string] [--x=int] "
            "[--flag|--no-flag] [file...]\n");

  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, Help) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* help[] = {const_cast<char*>(__FILE__), const_cast<char*>("-h")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  ASSERT_EQ(0, gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(help), help));

  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, Badargs1) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_arg_name[] = {const_cast<char*>(__FILE__),
                          const_cast<char*>("--y")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  ASSERT_EQ(0,
            gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_arg_name), bad_arg_name));

  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, Badargs2) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_int_value[] = {const_cast<char*>(__FILE__),
                           const_cast<char*>("--x"),
                           const_cast<char*>("henry")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  ASSERT_EQ(
      0, gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_int_value), bad_int_value));

  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, Badargs3) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_bool_value[] = {const_cast<char*>(__FILE__),
                            const_cast<char*>("--flag=henry")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  ASSERT_EQ(
      0, gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_bool_value), bad_bool_value));

  gpr_cmdline_destroy(cl);
}

TEST(CmdlineTest, Badargs4) {
  gpr_cmdline* cl;

  const char* str = nullptr;
  int x = 0;
  int flag = 2;

  char* bad_bool_value[] = {const_cast<char*>(__FILE__),
                            const_cast<char*>("--no-str")};

  LOG_TEST();

  cl = gpr_cmdline_create(nullptr);
  gpr_cmdline_set_survive_failure(cl);
  gpr_cmdline_add_string(cl, "str", nullptr, &str);
  gpr_cmdline_add_int(cl, "x", nullptr, &x);
  gpr_cmdline_add_flag(cl, "flag", nullptr, &flag);
  gpr_cmdline_on_extra_arg(cl, "file", "filenames to process", extra_arg_cb,
                           nullptr);

  ASSERT_EQ(
      0, gpr_cmdline_parse(cl, GPR_ARRAY_SIZE(bad_bool_value), bad_bool_value));

  gpr_cmdline_destroy(cl);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
