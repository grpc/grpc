// Copyright 2025 gRPC authors.
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

#include "src/core/channelz/zviz/trace.h"

#include <google/protobuf/text_format.h>

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "test/core/channelz/zviz/environment_fake.h"
#include "test/core/channelz/zviz/layout_log.h"

namespace grpc_zviz {
namespace {

void FormatTraceEventsDoesNotCrash(
    std::vector<grpc::channelz::v2::TraceEvent> events,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  EnvironmentFake env(std::move(entities));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  auto& table = element.AppendTable(layout::TableIntent::kTrace);
  for (const auto& event : events) {
    Format(env, event, table);
    table.NewRow();
  }
}
FUZZ_TEST(TraceTest, FormatTraceEventsDoesNotCrash);

void ExpectTraceEventsTransformsTo(std::string proto, std::string expected) {
  EnvironmentFake env({});
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  auto& table = element.AppendTable(layout::TableIntent::kTrace);
  grpc::channelz::v2::TraceEvent event;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &event));
  Format(env, event, table);
  EXPECT_EQ(expected, absl::StrJoin(lines, "\n")) << "TRACE: " << proto;
}

TEST(TraceTest, ChangeDetectors) {
  ExpectTraceEventsTransformsTo(
      R"pb(
        description: "foo"
      )pb",
      R"([0] APPEND_TABLE trace
[0] [0,0] APPEND_COLUMN
[0] [0,0] APPEND_TEXT timestamp [value elided]
[0] [1,0] APPEND_COLUMN
[0] [1,0] APPEND_TEXT trace-description foo)");
}

}  // namespace
}  // namespace grpc_zviz
