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

#ifndef GRPC_SRC_CORE_CHANNELZ_PROPERTY_LIST_H
#define GRPC_SRC_CORE_CHANNELZ_PROPERTY_LIST_H

#include <type_traits>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/util/json/json.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"

namespace grpc_core::channelz {

// PropertyList contains a bag of key->value (for mostly arbitrary value types)
// for reporting out state from channelz - the big idea is that you should be
// able to call `PropertyList().Set("a", this->a)` and generate something that
// channelz presenters can interpret.
// It's currently defined in terms of JSON, but as we adopt channelz-v2 it's
// expected we'll change this to capture protobuf directly.
class PropertyList {
 public:
  PropertyList& Set(absl::string_view key, absl::string_view value) {
    property_list_.emplace(key, Json::FromString(std::string(value)));
    return *this;
  }

  PropertyList& Set(absl::string_view key, const char* value) {
    if (value != nullptr) {
      property_list_.emplace(key, Json::FromString(std::string(value)));
    } else {
      property_list_.erase(std::string(key));
    }
    return *this;
  }

  PropertyList& Set(absl::string_view key, std::string value) {
    property_list_.emplace(key, Json::FromString(std::move(value)));
    return *this;
  }

  template <typename T>
  std::enable_if_t<std::is_arithmetic_v<T>, PropertyList&> Set(
      absl::string_view key, T value) {
    property_list_.emplace(key, Json::FromNumber(value));
    return *this;
  }

  PropertyList& Set(absl::string_view key, Json::Object obj) {
    property_list_.emplace(key, Json::FromObject(std::move(obj)));
    return *this;
  }

  PropertyList& Set(absl::string_view key, Duration dur) {
    property_list_.emplace(key, Json::FromString(dur.ToJsonString()));
    return *this;
  }

  PropertyList& Set(absl::string_view key, Timestamp ts) {
    property_list_.emplace(key, Json::FromString(gpr_format_timespec(
                                    ts.as_timespec(GPR_CLOCK_REALTIME))));
    return *this;
  }

  PropertyList& Set(absl::string_view key, absl::Status status) {
    property_list_.emplace(key, Json::FromString(status.ToString()));
    return *this;
  }

  template <typename T>
  PropertyList& Set(absl::string_view key, std::optional<T> value) {
    if (value.has_value()) {
      Set(key, *std::move(value));
    } else {
      property_list_.erase(std::string(key));
    }
    return *this;
  }

  // TODO(ctiller): remove soon, switch to something returning a protobuf.
  Json::Object TakeJsonObject() { return std::move(property_list_); }

 private:
  // TODO(ctiller): switch to a protobuf representation
  Json::Object property_list_;
};

}  // namespace grpc_core::channelz

#endif  // GRPC_SRC_CORE_CHANNELZ_PROPERTY_LIST_H
