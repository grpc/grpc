// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H
#define GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H

#include <grpc/context_types.h>

#include <string_view>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/util/unique_type_name.h"

namespace grpc_core {

template <>
struct ArenaContextType<TelemetryLabel> {
  static void Destroy(TelemetryLabel*) {}
};

class TelemetryLabelAttribute
    : public ServiceConfigCallData::CallAttributeInterface {
 public:
  explicit TelemetryLabelAttribute(std::string_view value) : value_(value) {}

  std::string_view value() const { return value_; }

  static UniqueTypeName TypeName() {
    static const UniqueTypeName::Factory factory("telemetry_label");
    return factory.Create();
  }

 private:
  UniqueTypeName type() const override { return TypeName(); }

  std::string_view value_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H
