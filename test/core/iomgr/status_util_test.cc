//
// Copyright 2021 the gRPC authors.
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

#include "src/core/lib/iomgr/status_util.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"

namespace grpc_core {
namespace {

TEST(StatusUtilTest, CreateStatus) {
  absl::Status s = grpc_status_create(
      absl::StatusCode::kUnknown, "Test", "status_util_test.cc", 10,
      {absl::OkStatus(), absl::CancelledError()});
  EXPECT_EQ(absl::StatusCode::kUnknown, s.code());
  EXPECT_EQ("Test", s.message());
  EXPECT_EQ("status_util_test.cc", grpc_status_get_str(s, GRPC_ERROR_STR_FILE));
  EXPECT_EQ(10, grpc_status_get_int(s, GRPC_ERROR_INT_FILE_LINE));
  EXPECT_EQ(true, s.GetPayload("created").has_value());
  EXPECT_EQ("CANCELLED", std::string(*s.GetPayload("children")));
}

TEST(StatusUtilTest, SetAndGetInt) {
  absl::Status s = absl::CancelledError();
  grpc_status_set_int(&s, GRPC_ERROR_INT_ERRNO, 2021);
  EXPECT_EQ(2021, grpc_status_get_int(s, GRPC_ERROR_INT_ERRNO));
}

TEST(StatusUtilTest, GetIntNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(absl::optional<intptr_t>(),
            grpc_status_get_int(s, GRPC_ERROR_INT_ERRNO));
}

TEST(StatusUtilTest, SetAndGetStr) {
  absl::Status s = absl::CancelledError();
  grpc_status_set_str(&s, GRPC_ERROR_STR_DESCRIPTION, "str");
  EXPECT_EQ("str", grpc_status_get_str(s, GRPC_ERROR_STR_DESCRIPTION));
}

TEST(StatusUtilTest, GetStrNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(absl::optional<std::string>(),
            grpc_status_get_str(s, GRPC_ERROR_STR_DESCRIPTION));
}

TEST(StatusUtilTest, AddChild) {
  absl::Status s = absl::CancelledError();
  grpc_status_add_child(&s, absl::AbortedError("Message1"));
  grpc_status_add_child(&s, absl::DeadlineExceededError("Message2"));
  EXPECT_EQ("ABORTED:Message1, DEADLINE_EXCEEDED:Message2",
            std::string(*s.GetPayload("children")));
}

TEST(StatusUtilTest, OkToString) {
  absl::Status s = absl::OkStatus();
  std::string t = grpc_status_to_string(s);
  EXPECT_EQ("OK", t);
}

TEST(StatusUtilTest, CancelledErrorToString) {
  absl::Status s = absl::CancelledError();
  std::string t = grpc_status_to_string(s);
  EXPECT_EQ("CANCELLED", t);
}

TEST(StatusUtilTest, ComplexErrorToString) {
  absl::Status s = absl::CancelledError("Message");
  grpc_status_set_int(&s, GRPC_ERROR_INT_ERRNO, 2021);
  std::string t = grpc_status_to_string(s);
  EXPECT_EQ("CANCELLED:Message {errno:'2021'}", t);
}

TEST(StatusUtilTest, ComplexErrorWithChildrenToString) {
  absl::Status s = absl::CancelledError("Message");
  grpc_status_set_int(&s, GRPC_ERROR_INT_ERRNO, 2021);
  absl::Status s1 = absl::AbortedError("Message1");
  grpc_status_add_child(&s, s1);
  absl::Status s2 = absl::AlreadyExistsError("Message2");
  grpc_status_set_str(&s2, GRPC_ERROR_STR_DESCRIPTION, "str");
  grpc_status_add_child(&s, s2);
  std::string t = grpc_status_to_string(s);
  EXPECT_EQ(
      "CANCELLED:Message {errno:'2021', children:["
      "ABORTED:Message1, ALREADY_EXISTS:Message2 {description:'str'}]}",
      t);
}

TEST(StatusUtilTest, LogStatus) {
  gpr_log_verbosity_init();
  gpr_set_log_function([](gpr_log_func_args* args) {
    EXPECT_STREQ("status_util_test.cc", args->file);
    EXPECT_EQ(10, args->line);
    EXPECT_EQ(GPR_LOG_SEVERITY_ERROR, args->severity);
    EXPECT_STREQ("what: CANCELLED", args->message);
  });
  grpc_log_status("what", absl::CancelledError(), "status_util_test.cc", 10);
  gpr_set_log_function(nullptr);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
