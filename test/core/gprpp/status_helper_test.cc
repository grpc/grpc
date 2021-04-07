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

#include "src/core/lib/gprpp/status_helper.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "google/rpc/status.upb.h"
#include "upb/upb.hpp"

namespace grpc_core {
namespace {

static absl::string_view kCreatedUrl =
    "type.googleapis.com/grpc.status.created";
static absl::string_view kIntField = "int";
static absl::string_view kStrField = "str";

#ifndef NDEBUG
static absl::string_view kFileField = "file";
static absl::string_view kFileLineField = "file_line";
#endif

TEST(StatusUtilTest, CreateStatus) {
  absl::Status s =
      StatusCreate(absl::StatusCode::kUnknown, "Test", DEBUG_LOCATION,
                   {absl::OkStatus(), absl::CancelledError()});
  EXPECT_EQ(absl::StatusCode::kUnknown, s.code());
  EXPECT_EQ("Test", s.message());
#ifndef NDEBUG
  EXPECT_EQ(true, StatusGetStr(s, kFileField).has_value());
  EXPECT_EQ(true, StatusGetInt(s, kFileLineField).has_value());
#endif
  EXPECT_EQ(true, s.GetPayload(kCreatedUrl).has_value());
  EXPECT_EQ(std::vector<absl::Status>({absl::CancelledError()}),
            StatusGetChildren(s));
}

TEST(StatusUtilTest, SetAndGetInt) {
  absl::Status s = absl::CancelledError();
  StatusSetInt(&s, kIntField, 2021);
  EXPECT_EQ(2021, StatusGetInt(s, kIntField));
}

TEST(StatusUtilTest, GetIntNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(absl::optional<intptr_t>(), StatusGetInt(s, kIntField));
}

TEST(StatusUtilTest, SetAndGetStr) {
  absl::Status s = absl::CancelledError();
  StatusSetStr(&s, kStrField, "value");
  EXPECT_EQ("value", StatusGetStr(s, kStrField));
}

TEST(StatusUtilTest, GetStrNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(absl::optional<std::string>(), StatusGetStr(s, kStrField));
}

TEST(StatusUtilTest, AddAndGetChildren) {
  absl::Status s = absl::CancelledError();
  absl::Status child1 = absl::AbortedError("Message1");
  absl::Status child2 = absl::DeadlineExceededError("Message2");
  StatusAddChild(&s, child1);
  StatusAddChild(&s, child2);
  EXPECT_EQ(std::vector<absl::Status>({child1, child2}), StatusGetChildren(s));
}

TEST(StatusUtilTest, ToAndFromProto) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, kIntField, 2021);
  StatusSetStr(&s, kStrField, "value");
  upb::Arena arena;
  google_rpc_Status* msg = StatusToProto(s, arena.ptr());
  absl::Status s2 = StatusFromProto(msg);
  EXPECT_EQ(s, s2);
}

TEST(StatusUtilTest, OkToString) {
  absl::Status s = absl::OkStatus();
  std::string t = StatusToString(s);
  EXPECT_EQ("OK", t);
}

TEST(StatusUtilTest, CancelledErrorToString) {
  absl::Status s = absl::CancelledError();
  std::string t = StatusToString(s);
  EXPECT_EQ("CANCELLED", t);
}

TEST(StatusUtilTest, ComplexErrorToString) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, kIntField, 2021);
  std::string t = StatusToString(s);
  EXPECT_EQ("CANCELLED:Message {int:'2021'}", t);
}

TEST(StatusUtilTest, ComplexErrorWithChildrenToString) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, kIntField, 2021);
  absl::Status s1 = absl::AbortedError("Message1");
  StatusAddChild(&s, s1);
  absl::Status s2 = absl::AlreadyExistsError("Message2");
  StatusSetStr(&s2, kStrField, "value");
  StatusAddChild(&s, s2);
  std::string t = StatusToString(s);
  EXPECT_EQ(
      "CANCELLED:Message {int:'2021', children:["
      "ABORTED:Message1, ALREADY_EXISTS:Message2 {str:'value'}]}",
      t);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
