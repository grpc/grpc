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

#include "src/core/channelz/zviz/entity.h"

#include "src/core/channelz/zviz/data.h"
#include "src/core/channelz/zviz/strings.h"
#include "src/core/channelz/zviz/trace.h"
#include "absl/strings/str_cat.h"

namespace grpc_zviz {

void Format(Environment& env, const grpc::channelz::v2::Entity& entity,
            layout::Element& element) {
  element.AppendText(
      layout::Intent::kBanner,
      absl::StrCat(entity.orphaned() ? "Orphaned " : "",
                   DisplayKind(entity.kind()), " ", entity.id()));
  if (entity.parents_size() > 0) {
    auto& grp = element.AppendGroup(layout::Intent::kHeading);
    grp.AppendText(layout::Intent::kHeading, "Parents:");
    for (int64_t parent_id : entity.parents()) {
      grp.AppendEntityLink(env, parent_id);
    }
  }
  if (entity.trace_size() > 0) {
    auto& grp = element.AppendGroup(layout::Intent::kTrace);
    grp.AppendText(layout::Intent::kHeading, "Trace:");
    auto& table = grp.AppendTable(layout::TableIntent::kTrace);
    for (const auto& trace : entity.trace()) {
      Format(env, trace, table);
      table.NewRow();
    }
  }
  for (const auto& data : entity.data()) {
    Format(env, data, element);
  }
}

}  // namespace grpc_zviz
