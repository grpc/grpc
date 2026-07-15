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

#include "src/core/channelz/zviz/environment.h"

#include "src/core/channelz/zviz/strings.h"

namespace grpc_zviz {

std::string Environment::EntityLinkText(int64_t entity_id) {
  auto entity = GetEntity(entity_id);
  std::string kind;
  if (!entity.ok()) {
    kind = "Entity";
  } else {
    kind = DisplayKind(entity->kind());
  }
  return absl::StrCat(kind, " ", entity_id);
}

}  // namespace grpc_zviz
