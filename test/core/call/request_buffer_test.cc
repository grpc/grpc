// Copyright 2024 gRPC authors.
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

#include "src/core/call/request_buffer.h"

#include "gtest/gtest.h"

#include "test/core/promise/poll_matcher.h"

namespace grpc_core {

namespace {
void CrashOnParseError(absl::string_view error, const Slice& data) {
  LOG(FATAL) << "Failed to parse " << error << " from "
             << data.as_string_view();
}
}  // namespace

TEST(RequestBufferTest, NoOp) { RequestBuffer buffer; }

TEST(RequestBufferTest, PushThenPullClientInitialMetadata) {
  RequestBuffer buffer;
  ClientMetadataHandle md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Append("key", Slice::FromStaticString("value"), CrashOnParseError);
  ASSERT_EQ(buffer.PushClientInitialMetadata(std::move(md)), Success{});
  RequestBuffer::Reader reader(&buffer);
  auto poll = reader.PullClientInitialMetadata()();
  ASSERT_THAT(poll, IsReady());
  auto value = std::move(poll.value());
  ASSERT_TRUE(value.ok());
  std::string backing;
  EXPECT_EQ((*value)->GetStringValue("key", &backing), "value");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
