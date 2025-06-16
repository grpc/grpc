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

#include <cstddef>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/util/json/json.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"

namespace grpc_core::channelz {

namespace property_list_detail {

template <typename T, typename = void>
struct JsonFromValueHelper;

template <>
struct JsonFromValueHelper<absl::string_view> {
  static std::optional<Json> JsonFromValue(absl::string_view value) {
    return Json::FromString(std::string(value));
  }
};

template <>
struct JsonFromValueHelper<const char*> {
  static std::optional<Json> JsonFromValue(const char* value) {
    if (value != nullptr) {
      return Json::FromString(std::string(value));
    } else {
      return std::nullopt;
    }
  }
};

template <>
struct JsonFromValueHelper<std::string> {
  static std::optional<Json> JsonFromValue(std::string value) {
    return Json::FromString(std::move(value));
  }
};

template <typename T>
struct JsonFromValueHelper<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
  static std::optional<Json> JsonFromValue(T value) {
    return Json::FromNumber(value);
  }
};

template <>
struct JsonFromValueHelper<Json::Object> {
  static std::optional<Json> JsonFromValue(Json::Object value) {
    return Json::FromObject(std::move(value));
  }
};

template <>
struct JsonFromValueHelper<Json::Array> {
  static std::optional<Json> JsonFromValue(Json::Array value) {
    return Json::FromArray(std::move(value));
  }
};

template <>
struct JsonFromValueHelper<Json> {
  static std::optional<Json> JsonFromValue(Json value) {
    return std::move(value);
  }
};

template <>
struct JsonFromValueHelper<Duration> {
  static std::optional<Json> JsonFromValue(Duration dur) {
    return Json::FromString(dur.ToJsonString());
  }
};

template <>
struct JsonFromValueHelper<Timestamp> {
  static std::optional<Json> JsonFromValue(Timestamp ts) {
    return Json::FromString(
        gpr_format_timespec(ts.as_timespec(GPR_CLOCK_REALTIME)));
  }
};

template <>
struct JsonFromValueHelper<absl::Status> {
  static std::optional<Json> JsonFromValue(absl::Status status) {
    return Json::FromString(status.ToString());
  }
};

template <typename T>
struct JsonFromValueHelper<std::optional<T>> {
  static std::optional<Json> JsonFromValue(std::optional<T> value) {
    if (value.has_value()) {
      return JsonFromValueHelper<T>::JsonFromValue(*std::move(value));
    } else {
      return std::nullopt;
    }
  }
};

}  // namespace property_list_detail

// PropertyList contains a bag of key->value (for mostly arbitrary value
// types) for reporting out state from channelz - the big idea is that you
// should be able to call `PropertyList().Set("a", this->a)` and generate
// something that channelz presenters can interpret. It's currently defined in
// terms of JSON, but as we adopt channelz-v2 it's expected we'll change this
// to capture protobuf directly.
class PropertyList {
 public:
  template <typename T>
  PropertyList& Set(absl::string_view key, T value) {
    SetInternal(
        key,
        property_list_detail::JsonFromValueHelper<T>::JsonFromValue(value));
    return *this;
  }

  // TODO(ctiller): remove soon, switch to something returning a protobuf.
  Json::Object TakeJsonObject() { return std::move(property_list_); }

 private:
  void SetInternal(absl::string_view key, std::optional<Json> value);

  // TODO(ctiller): switch to a protobuf representation
  Json::Object property_list_;
};

namespace property_list_detail {

template <>
struct JsonFromValueHelper<PropertyList> {
  static std::optional<Json> JsonFromValue(PropertyList value) {
    return Json::FromObject(value.TakeJsonObject());
  }
};

template <>
struct JsonFromValueHelper<std::vector<PropertyList>> {
  static std::optional<Json> JsonFromValue(std::vector<PropertyList> values) {
    Json::Array array;
    for (auto& v : values) {
      array.emplace_back(Json::FromObject(v.TakeJsonObject()));
    }
    return Json::FromArray(std::move(array));
  }
};

}  // namespace property_list_detail

}  // namespace grpc_core::channelz

#endif  // GRPC_SRC_CORE_CHANNELZ_PROPERTY_LIST_H
