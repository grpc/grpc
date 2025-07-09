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

#ifndef GRPC_SRC_CORE_CHANNELZ_V2TOV1_PROPERTY_LIST_H
#define GRPC_SRC_CORE_CHANNELZ_V2TOV1_PROPERTY_LIST_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"

// Forward declarations for UPB types.
struct grpc_channelz_v2_PropertyList;
struct upb_Arena;
struct google_protobuf_Timestamp;
struct google_protobuf_Duration;

namespace grpc_core {
namespace channelz {
namespace v2tov1 {

// Helpers for extracting values from a PropertyList.
std::optional<int64_t> Int64FromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name);
std::optional<std::string> StringFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name);
const google_protobuf_Timestamp* TimestampFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name);
const grpc_channelz_v2_PropertyList* PropertyListFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name,
    upb_Arena* arena);
const google_protobuf_Duration* DurationFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name);

}  // namespace v2tov1
}  // namespace channelz
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CHANNELZ_V2TOV1_PROPERTY_LIST_H
