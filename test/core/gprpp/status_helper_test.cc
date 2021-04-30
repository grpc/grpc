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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "google/rpc/status.upb.h"
#include "upb/upb.hpp"

namespace grpc_core {
namespace {

TEST(StatusUtilTest, CreateStatus) {
  absl::Status s =
      StatusCreate(absl::StatusCode::kUnknown, "Test", DEBUG_LOCATION,
                   {absl::OkStatus(), absl::CancelledError()});
  EXPECT_EQ(absl::StatusCode::kUnknown, s.code());
  EXPECT_EQ("Test", s.message());
#ifndef NDEBUG
  EXPECT_EQ(true, StatusGetStr(s, StatusStrProperty::kFile).has_value());
  EXPECT_EQ(true, StatusGetInt(s, StatusIntProperty::kFileLine).has_value());
#endif
  EXPECT_EQ(true, StatusGetTime(s, StatusTimeProperty::kCreated).has_value());
  EXPECT_THAT(StatusGetChildren(s),
              ::testing::ElementsAre(absl::CancelledError()));
}

TEST(StatusUtilTest, SetAndGetInt) {
  absl::Status s = absl::CancelledError();
  StatusSetInt(&s, StatusIntProperty::kErrorNo, 2021);
  EXPECT_EQ(2021, StatusGetInt(s, StatusIntProperty::kErrorNo));
}

TEST(StatusUtilTest, GetIntNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(absl::optional<intptr_t>(),
            StatusGetInt(s, StatusIntProperty::kErrorNo));
}

TEST(StatusUtilTest, SetAndGetStr) {
  absl::Status s = absl::CancelledError();
  StatusSetStr(&s, StatusStrProperty::kOsError, "value");
  EXPECT_EQ("value", StatusGetStr(s, StatusStrProperty::kOsError));
}

TEST(StatusUtilTest, GetStrNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(absl::optional<std::string>(),
            StatusGetStr(s, StatusStrProperty::kOsError));
}

TEST(StatusUtilTest, SetAndGetTime) {
  absl::Status s = absl::CancelledError();
  absl::Time t = absl::Now();
  StatusSetTime(&s, StatusTimeProperty::kCreated, t);
  EXPECT_EQ(t, StatusGetTime(s, StatusTimeProperty::kCreated));
}

TEST(StatusUtilTest, GetTimeNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(absl::optional<absl::Time>(),
            StatusGetTime(s, StatusTimeProperty::kCreated));
}

TEST(StatusUtilTest, AddAndGetChildren) {
  absl::Status s = absl::CancelledError();
  absl::Status child1 = absl::AbortedError("Message1");
  absl::Status child2 = absl::DeadlineExceededError("Message2");
  StatusAddChild(&s, child1);
  StatusAddChild(&s, child2);
  EXPECT_THAT(StatusGetChildren(s), ::testing::ElementsAre(child1, child2));
}

TEST(StatusUtilTest, ToAndFromProto) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, StatusIntProperty::kErrorNo, 2021);
  StatusSetStr(&s, StatusStrProperty::kOsError, "value");
  upb::Arena arena;
  google_rpc_Status* msg = internal::StatusToProto(s, arena.ptr());
  absl::Status s2 = internal::StatusFromProto(msg);
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

TEST(StatusUtilTest, ErrorWithIntPropertyToString) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, StatusIntProperty::kErrorNo, 2021);
  std::string t = StatusToString(s);
  EXPECT_EQ("CANCELLED:Message {errno:2021}", t);
}

TEST(StatusUtilTest, ErrorWithStrPropertyToString) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetStr(&s, StatusStrProperty::kDescription, "Hey");
  std::string t = StatusToString(s);
  EXPECT_EQ("CANCELLED:Message {description:\"Hey\"}", t);
}

TEST(StatusUtilTest, ErrorWithTimePropertyToString) {
  absl::Status s = absl::CancelledError("Message");
  absl::Time t = absl::FromCivil(absl::CivilSecond(2021, 4, 29, 8, 56, 30),
                                 absl::LocalTimeZone());
  StatusSetTime(&s, StatusTimeProperty::kCreated, t);
  EXPECT_EQ(StatusToString(s),
            absl::StrCat("CANCELLED:Message {created_time:\"",
                         absl::FormatTime(t), "\"}"));
}

TEST(StatusUtilTest, ComplexErrorWithChildrenToString) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, StatusIntProperty::kErrorNo, 2021);
  absl::Status s1 = absl::AbortedError("Message1");
  StatusAddChild(&s, s1);
  absl::Status s2 = absl::AlreadyExistsError("Message2");
  StatusSetStr(&s2, StatusStrProperty::kOsError, "value");
  StatusAddChild(&s, s2);
  std::string t = StatusToString(s);
  EXPECT_EQ(
      "CANCELLED:Message {errno:2021, children:["
      "ABORTED:Message1, ALREADY_EXISTS:Message2 {os_error:\"value\"}]}",
      t);
}

TEST(StatusUtilTest, AllocPtr) {
  absl::Status statuses[] = {absl::OkStatus(), absl::CancelledError(),
                             absl::AbortedError("Message")};
  for (const auto& s : statuses) {
    uintptr_t p = internal::StatusAllocPtr(s);
    EXPECT_EQ(s, internal::StatusGetFromPtr(p));
    internal::StatusFreePtr(p);
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
