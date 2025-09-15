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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZVIZ_PROPERTY_LIST_H
#define GRPC_SRC_CORE_CHANNELZ_ZVIZ_PROPERTY_LIST_H

#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"

namespace grpc_zviz {

// Retrieves a property from an Entity proto using a dot-separated path.
//
// Example: "call_counts.calls_started" means look up data "call_counts",
// see that it is a PropertyList, then look up a key "calls_started"
// in that property list.
//
// If any item along the path cannot be found, returns nullopt.
// If multiple data sections in the entity have the same name, we look at each
// in turn; if there's a match we return the first one.
std::optional<std::string> GetPropertyAsString(
    const grpc::channelz::v2::Entity& entity, absl::string_view path);

}  // namespace grpc_zviz

#endif  // GRPC_SRC_CORE_CHANNELZ_ZVIZ_PROPERTY_LIST_H
