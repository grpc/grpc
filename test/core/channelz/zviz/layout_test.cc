// Copyright 2025 The gRPC Authors
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

#include "src/core/channelz/zviz/layout.h"

#include "src/core/channelz/zviz/environment.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace grpc_zviz::layout {
namespace {

class MockElement : public Element {
 public:
  MOCK_METHOD(Element&, AppendText, (Intent, absl::string_view), (override));
  MOCK_METHOD(Element&, AppendLink,
              (Intent, absl::string_view, absl::string_view), (override));
  MOCK_METHOD(Element&, AppendGroup, (Intent), (override));
  MOCK_METHOD(Element&, AppendData,
              (absl::string_view name, absl::string_view type), (override));
  MOCK_METHOD(Table&, AppendTable, (TableIntent), (override));
};

class MockEnvironment : public Environment {
 public:
  MOCK_METHOD(std::string, EntityLinkTarget, (int64_t), (override));
  MOCK_METHOD(absl::StatusOr<grpc::channelz::v2::Entity>, GetEntity, (int64_t),
              (override));
};

TEST(LayoutTest, AppendTimestamp) {
  StrictMock<MockElement> element;
  EXPECT_CALL(element, AppendText(Intent::kTimestamp, HasSubstr("2024-08")))
      .WillOnce(ReturnRef(element));
  google::protobuf::Timestamp timestamp;
  timestamp.set_seconds(1724200096);
  element.AppendTimestamp(timestamp);
}

TEST(LayoutTest, AppendDuration) {
  StrictMock<MockElement> element;
  EXPECT_CALL(element, AppendText(Intent::kDuration, "1m43.456789s"))
      .WillOnce(ReturnRef(element));
  google::protobuf::Duration duration;
  duration.set_seconds(103);
  duration.set_nanos(456789000);
  element.AppendDuration(duration);
}

TEST(LayoutTest, AppendEntityLink) {
  StrictMock<MockElement> element;
  StrictMock<MockEnvironment> env;
  grpc::channelz::v2::Entity entity;
  entity.set_kind("channel");
  EXPECT_CALL(env, GetEntity(123)).WillOnce(Return(entity));
  EXPECT_CALL(env, EntityLinkTarget(123))
      .WillOnce(Return("http://example.com/123"));
  EXPECT_CALL(element, AppendLink(Intent::kEntityRef, "Channel 123",
                                  "http://example.com/123"))
      .WillOnce(ReturnRef(element));
  element.AppendEntityLink(env, 123);
}

}  // namespace
}  // namespace grpc_zviz::layout
