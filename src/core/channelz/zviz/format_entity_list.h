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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZVIZ_FORMAT_ENTITY_LIST_H
#define GRPC_SRC_CORE_CHANNELZ_ZVIZ_FORMAT_ENTITY_LIST_H

#include <string>

#include "absl/types/span.h"
#include "src/core/channelz/zviz/environment.h"
#include "src/core/channelz/zviz/layout.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"

namespace grpc_zviz {

struct EntityTableColumn {
  absl::string_view title;
  absl::string_view property_path;
};

void FormatEntityList(Environment& env,
                      absl::Span<const grpc::channelz::v2::Entity> entities,
                      absl::Span<const EntityTableColumn> columns,
                      layout::Element& target);

}  // namespace grpc_zviz

#endif  // GRPC_SRC_CORE_CHANNELZ_ZVIZ_FORMAT_ENTITY_LIST_H
