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

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "src/core/channelz/zviz/layout.h"
#include "src/core/channelz/zviz/property_list.h"

namespace grpc_zviz {

void FormatEntityList(Environment& env,
                      absl::Span<const grpc::channelz::v2::Entity> entities,
                      absl::Span<const EntityTableColumn> columns,
                      layout::Element& target) {
  auto& table = target.AppendTable(layout::TableIntent::kPropertyTable);
  for (const auto& column : columns) {
    table.AppendColumn().AppendText(layout::Intent::kHeading, column.title);
  }
  table.NewRow();

  for (const auto& entity : entities) {
    for (const auto& column : columns) {
      auto& cell = table.AppendColumn();
      absl::string_view path = column.property_path;
      if (absl::ConsumePrefix(&path, "link:")) {
        auto text = GetPropertyAsString(entity, path);
        cell.AppendLink(layout::Intent::kValue,
                        text.value_or(env.EntityLinkText(entity.id())),
                        env.EntityLinkTarget(entity.id()));
      } else if (absl::ConsumePrefix(&path, "children_of_kind:")) {
        auto children = env.GetChildren(entity.id(), path);
        if (!children.ok()) {
          cell.AppendText(layout::Intent::kValue, "<error>");
        } else {
          bool first = true;
          for (const auto& child : *children) {
            if (!first) {
              cell.AppendText(layout::Intent::kValue, ", ");
            }
            first = false;
            cell.AppendLink(layout::Intent::kValue,
                            env.EntityLinkText(child.id()),
                            env.EntityLinkTarget(child.id()));
          }
        }
      } else {
        cell.AppendText(layout::Intent::kValue,
                        GetPropertyAsString(entity, path).value_or(""));
      }
    }
    table.NewRow();
  }
}

}  // namespace grpc_zviz
