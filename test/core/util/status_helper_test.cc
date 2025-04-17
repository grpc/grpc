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

#include "src/core/util/status_helper.h"

#include <stddef.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "google/rpc/status.upb.h"
#include "gtest/gtest.h"
#include "upb/mem/arena.hpp"

namespace grpc_core {
namespace {

TEST(StatusUtilTest, CreateStatus) {
  absl::Status s =
      StatusCreate(absl::StatusCode::kUnknown, "Test", DEBUG_LOCATION,
                   {absl::OkStatus(), absl::CancelledError()});
  EXPECT_EQ(absl::StatusCode::kUnknown, s.code());
  EXPECT_EQ("Test", s.message());
  EXPECT_THAT(StatusGetChildren(s),
              ::testing::ElementsAre(absl::CancelledError()));
}

TEST(StatusUtilTest, SetAndGetInt) {
  absl::Status s = absl::CancelledError();
  StatusSetInt(&s, StatusIntProperty::kStreamId, 2021);
  EXPECT_EQ(2021, StatusGetInt(s, StatusIntProperty::kStreamId));
}

TEST(StatusUtilTest, GetIntNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(std::optional<intptr_t>(),
            StatusGetInt(s, StatusIntProperty::kStreamId));
}

TEST(StatusUtilTest, SetAndGetStr) {
  absl::Status s = absl::CancelledError();
  StatusSetStr(&s, StatusStrProperty::kGrpcMessage, "value");
  EXPECT_EQ("value", StatusGetStr(s, StatusStrProperty::kGrpcMessage));
}

TEST(StatusUtilTest, GetStrNotExistent) {
  absl::Status s = absl::CancelledError();
  EXPECT_EQ(std::optional<std::string>(),
            StatusGetStr(s, StatusStrProperty::kGrpcMessage));
}

TEST(StatusUtilTest, AddAndGetChildren) {
  absl::Status s = absl::CancelledError();
  absl::Status child1 = absl::AbortedError("Message1");
  absl::Status child2 = absl::DeadlineExceededError("Message2");
  absl::Status child3 = absl::UnimplementedError("");
  StatusAddChild(&s, child1);
  StatusAddChild(&s, child2);
  StatusAddChild(&s, child3);
  EXPECT_THAT(StatusGetChildren(s),
              ::testing::ElementsAre(child1, child2, child3));
}

TEST(StatusUtilTest, ToAndFromProto) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, StatusIntProperty::kStreamId, 2021);
  upb::Arena arena;
  google_rpc_Status* msg = internal::StatusToProto(s, arena.ptr());
  size_t len;
  const char* buf = google_rpc_Status_serialize(msg, arena.ptr(), &len);
  google_rpc_Status* msg2 = google_rpc_Status_parse(buf, len, arena.ptr());
  absl::Status s2 = internal::StatusFromProto(msg2);
  EXPECT_EQ(s, s2);
}

TEST(StatusUtilTest, ToAndFromProtoWithNonUTF8Characters) {
  absl::Status s = absl::CancelledError("_\xAB\xCD\xEF_");
  StatusSetInt(&s, StatusIntProperty::kStreamId, 2021);
  upb::Arena arena;
  google_rpc_Status* msg = internal::StatusToProto(s, arena.ptr());
  size_t len;
  const char* buf = google_rpc_Status_serialize(msg, arena.ptr(), &len);
  google_rpc_Status* msg2 = google_rpc_Status_parse(buf, len, arena.ptr());
  absl::Status s2 = internal::StatusFromProto(msg2);
  EXPECT_EQ(s, s2);
}

TEST(StatusUtilTest, OkToString) {
  absl::Status s;
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
  StatusSetInt(&s, StatusIntProperty::kStreamId, 2021);
  std::string t = StatusToString(s);
  EXPECT_EQ("CANCELLED:Message {stream_id:2021}", t);
}

TEST(StatusUtilTest, ErrorWithStrPropertyToString) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetStr(&s, StatusStrProperty::kGrpcMessage, "Hey");
  std::string t = StatusToString(s);
  EXPECT_EQ("CANCELLED:Message {grpc_message:\"Hey\"}", t);
}

TEST(StatusUtilTest, ComplexErrorWithChildrenToString) {
  absl::Status s = absl::CancelledError("Message");
  StatusSetInt(&s, StatusIntProperty::kStreamId, 2021);
  absl::Status s1 = absl::AbortedError("Message1");
  StatusAddChild(&s, s1);
  absl::Status s2 = absl::AlreadyExistsError("Message2");
  StatusSetStr(&s2, StatusStrProperty::kGrpcMessage, "value");
  StatusAddChild(&s, s2);
  std::string t = StatusToString(s);
  EXPECT_EQ(
      "CANCELLED:Message {stream_id:2021, children:["
      "ABORTED:Message1, ALREADY_EXISTS:Message2 {grpc_message:\"value\"}]}",
      t);
}

TEST(StatusUtilTest, AllocHeapPtr) {
  absl::Status statuses[] = {absl::OkStatus(), absl::CancelledError(),
                             absl::AbortedError("Message")};
  for (const auto& s : statuses) {
    uintptr_t p = internal::StatusAllocHeapPtr(s);
    EXPECT_EQ(s, internal::StatusGetFromHeapPtr(p));
    internal::StatusFreeHeapPtr(p);
  }
}

TEST(StatusUtilTest, MoveHeapPtr) {
  absl::Status statuses[] = {absl::OkStatus(), absl::CancelledError(),
                             absl::AbortedError("Message")};
  for (const auto& s : statuses) {
    uintptr_t p = internal::StatusAllocHeapPtr(s);
    EXPECT_EQ(s, internal::StatusMoveFromHeapPtr(p));
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
