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

#include "src/core/channelz/zviz/format_entity_list.h"

#include <google/protobuf/text_format.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/channelz/zviz/layout_html.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "test/core/channelz/zviz/environment_fake.h"
#include "test/core/channelz/zviz/layout_log.h"

namespace grpc_zviz {
namespace {

void FormatEntityListDoesNotCrash(
    std::vector<int64_t> entities_to_format,
    std::vector<EntityTableColumn> columns,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  std::vector<grpc::channelz::v2::Entity> entities_to_format_vector;
  for (int64_t entity_id : entities_to_format) {
    auto entity = entities.find(entity_id);
    if (entity == entities.end()) {
      continue;
    }
    entities_to_format_vector.push_back(entity->second);
  }
  EnvironmentFake env(std::move(entities));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  FormatEntityList(env, entities_to_format_vector, columns, element);
}
FUZZ_TEST(FormatEntityListTest, FormatEntityListDoesNotCrash);

// Helper to construct an Entity proto from a text-format string.
grpc::channelz::v2::Entity ParseEntity(absl::string_view proto) {
  grpc::channelz::v2::Entity msg;
  CHECK(
      google::protobuf::TextFormat::ParseFromString(std::string(proto), &msg));
  return msg;
}

void ExpectEntityListTransformsTo(std::vector<std::string> protos,
                                  std::vector<EntityTableColumn> columns,
                                  std::string expected) {
  absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entity_map;
  std::vector<grpc::channelz::v2::Entity> entities;
  for (const auto& proto : protos) {
    auto entity = ParseEntity(proto);
    entities.push_back(entity);
    entity_map[entity.id()] = entity;
  }
  // Also add children to the entity map so they can be found by GetChildren.
  for (const auto& proto : {
           R"pb(id: 456 parents: 123 kind: "socket")pb",
           R"pb(id: 789 parents: 123 kind: "socket")pb",
       }) {
    auto entity = ParseEntity(proto);
    entity_map[entity.id()] = entity;
  }
  EnvironmentFake env(std::move(entity_map));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  FormatEntityList(env, entities, columns, element);
  EXPECT_EQ(expected, absl::StrJoin(lines, "\n"))
      << "ENTITIES: " << absl::StrJoin(protos, "\n");
}

TEST(FormatEntityListTest, ChildrenOfKind) {
  ExpectEntityListTransformsTo(
      {
          R"pb(id: 123)pb",
      },
      {
          {"Children", "children_of_kind:socket"},
      },
      R"([0] APPEND_TABLE property-table
[0] [0,0] APPEND_COLUMN
[0] [0,0] APPEND_TEXT heading Children
[0] NEW_ROW
[0] [0,1] APPEND_COLUMN
[0] [0,1] APPEND_LINK value Socket 456 http://example.com/456
[0] [0,1] APPEND_TEXT value , 
[0] [0,1] APPEND_LINK value Socket 789 http://example.com/789
[0] NEW_ROW)");
}

TEST(FormatEntityListTest, ChangeDetectors) {
  ExpectEntityListTransformsTo(
      {
          R"pb(
            id: 123
            data: {
              name: "p1"
              value: {
                [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
                  properties: {
                    key: "status"
                    value: { string_value: "OK" }
                  }
                }
              }
            }
          )pb",
          R"pb(
            id: 456
            data: {
              name: "p1"
              value: {
                [type.googleapis.com/grpc.channelz.v2.PropertyList]: {
                  properties: {
                    key: "status"
                    value: { string_value: "ERROR" }
                  }
                }
              }
            }
          )pb",
      },
      {
          {"Status", "p1.status"},
      },
      R"([0] APPEND_TABLE property-table
[0] [0,0] APPEND_COLUMN
[0] [0,0] APPEND_TEXT heading Status
[0] NEW_ROW
[0] [0,1] APPEND_COLUMN
[0] [0,1] APPEND_TEXT value OK
[0] NEW_ROW
[0] [0,2] APPEND_COLUMN
[0] [0,2] APPEND_TEXT value ERROR
[0] NEW_ROW)");
}

}  // namespace
}  // namespace grpc_zviz
