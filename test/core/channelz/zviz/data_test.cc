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

#include "src/core/channelz/zviz/data.h"

#include <google/protobuf/text_format.h>

#include "absl/log/check.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "test/core/channelz/zviz/environment_fake.h"
#include "test/core/channelz/zviz/layout_log.h"

namespace grpc_zviz {
namespace {

void FormatAnyDoesNotCrash(
    google::protobuf::Any value,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  EnvironmentFake env(std::move(entities));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  Format(env, value, element);
}
FUZZ_TEST(DataTest, FormatAnyDoesNotCrash);

void FormatDatasDoesNotCrash(
    grpc::channelz::v2::Data data,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  EnvironmentFake env(std::move(entities));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  Format(env, data, element);
}
FUZZ_TEST(DataTest, FormatDatasDoesNotCrash);

void ExpectDataTransformsTo(std::string proto, std::string expected) {
  EnvironmentFake env({});
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  grpc::channelz::v2::Data data;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &data));
  Format(env, {data}, element);
  EXPECT_EQ(expected, absl::StrJoin(lines, "\n")) << "DATA: " << proto;
}

TEST(DataTest, ChangeDetectors) {
  ExpectDataTransformsTo(
      R"pb(
        name: "foo"
        value {
          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
            properties {
              key: "foo"
              value { string_value: "bar" }
            }
          }
        }
      )pb",
      R"([0] DATA foo type.googleapis.com/grpc.channelz.v2.PropertyList
[0] [0] APPEND_TABLE property_list
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT key foo
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] APPEND_TEXT value bar
[0] [0] NEW_ROW)");
}

}  // namespace
}  // namespace grpc_zviz
