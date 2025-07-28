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

#include "src/core/channelz/zviz/entity.h"

#include <google/protobuf/text_format.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"
#include "test/core/channelz/zviz/environment_fake.h"
#include "test/core/channelz/zviz/layout_log.h"

namespace grpc_zviz {
namespace {

void FormatEntityDoesNotCrash(
    grpc::channelz::v2::Entity entity,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  EnvironmentFake env(std::move(entities));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  Format(env, entity, element);
}
FUZZ_TEST(EntityTest, FormatEntityDoesNotCrash);

void ExpectEntityTransformsTo(std::string proto, std::string expected) {
  EnvironmentFake env({});
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  grpc::channelz::v2::Entity entity;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &entity));
  Format(env, entity, element);
  EXPECT_EQ(expected, absl::StrJoin(lines, "\n")) << "ENTITY: " << proto;
}

TEST(EntityTest, ChangeDetectors) {
  ExpectEntityTransformsTo(
      R"pb(
        id: 1
        kind: "channel"
        trace { description: "foo" }
        data {
          name: "bar"
          value {
            [type.googleapis.com/grpc.channelz.v2.PropertyList] {
              properties {
                key: "baz"
                value: { string_value: "qux" }
              }
            }
          }
        }
      )pb",
      R"(APPEND_TEXT banner Channel 1
[0] GROUP trace
[0] APPEND_TEXT heading Trace:
[0] [0] APPEND_TABLE trace
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT timestamp [value elided]
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] APPEND_TEXT trace-description foo
[0] [0] NEW_ROW
[1] DATA bar type.googleapis.com/grpc.channelz.v2.PropertyList
[1] [0] APPEND_TABLE property-list
[1] [0] [0,0] APPEND_COLUMN
[1] [0] [0,0] APPEND_TEXT key baz
[1] [0] [1,0] APPEND_COLUMN
[1] [0] [1,0] APPEND_TEXT value qux
[1] [0] NEW_ROW)");
}

}  // namespace
}  // namespace grpc_zviz
